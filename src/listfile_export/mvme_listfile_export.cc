#include <argh.h>
#include <cstring>
#include <QCoreApplication>
#include <QFileInfo>
#include <fstream>
#include <iostream>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <spdlog/spdlog.h>

#include "listfile_export/filter_db_from_templates.h"
#include "listfile_export/listfile_export.h"
#include "mvme_session.h"
#include "vme_config.h"
#include "vme_config_util.h"

using namespace mesytec;
using namespace mesytec::mvme::listfile_export;

static const char *UsageText = R"~(usage:
  mvme_listfile_export explore <listfile.zip>
  mvme_listfile_export export  <listfile.zip> [options] [<array>...]

Commands:
  explore   Show listfile meta info and all arrays available for export.
  export    Write the selected arrays to a table with one row per event and one
            column per array element. Arrays are selected by their full path as
            shown by 'explore', e.g. my_event.my_mdpp16.amplitude. Single elements
            are selected by appending a list of indices and inclusive ranges in
            brackets, e.g. 'my_event.my_mdpp16.amplitude[0-3,8]'. Quote these to
            avoid shell globbing. All arrays must belong to the same event. If no
            array is given and the listfile contains exactly one module with data,
            its first array is exported.

Export options:
  -o, --output <file>     Output file, '-' for stdout. Default: listfile name with .csv or
                          .txt extension.
  --format <csv|text>     csv: comma separated, missing values are empty.
                          text: space separated, missing values are 'nan'.
                          Default: from the output file extension, otherwise csv.
  --separator <sep>       Override the column separator. Any string, '\t' or 'tab' for a tab
                          character.
  --missing <nan|empty|0> Override the representation of missing values.
  --skip-empty-rows       Do not write events where all selected values are missing.
  --no-header             Do not write the column name header line.

General options:
  --log-level <level>     One of trace, debug, info, warn, error, off.
  -h, --help              Show this help.
)~";

struct ListfileContext
{
    std::string filename;
    std::unique_ptr<mvlc::listfile::ZipReader> zipReader;
    std::string entryName;
    mvlc::listfile::ReadHandle *readHandle = nullptr;
    mvlc::listfile::ListfileReaderHelper readerHelper;
    mvlc::CrateConfig crateConfig;
};

static std::unique_ptr<ListfileContext> open_listfile(const std::string &filename)
{
    auto ctx = std::make_unique<ListfileContext>();
    ctx->filename = filename;
    ctx->zipReader = std::make_unique<mvlc::listfile::ZipReader>();
    ctx->zipReader->openArchive(filename);
    ctx->entryName = ctx->zipReader->firstListfileEntryName();

    if (ctx->entryName.empty())
        throw std::runtime_error("no listfile entry found in archive");

    ctx->readHandle = ctx->zipReader->openEntry(ctx->entryName);
    ctx->readerHelper = mvlc::listfile::make_listfile_reader_helper(ctx->readHandle);

    const auto &preamble = ctx->readerHelper.preamble;
    auto crateConfigs = std::count_if(std::begin(preamble.systemEvents), std::end(preamble.systemEvents),
                                      [](const auto &se)
                                      { return se.type == mvlc::system_event::subtype::MVLCCrateConfig; });

    if (crateConfigs == 0)
        throw std::runtime_error("no MVLC crate config found in listfile (not an MVLC listfile?)");

    if (crateConfigs > 1)
        throw std::runtime_error("multi crate listfiles are not supported");

    ctx->crateConfig = mvlc::crate_config_from_yaml(preamble.findCrateConfig()->contentsToString());

    return ctx;
}

static FilterDb load_filter_db()
{
    return filter_db_from_templates(vats::read_templates());
}

// Module types from the MVMEConfig stored in the listfile preamble. Matches crate config stacks
// and groups to vme events and modules by name. Needed for listfiles written before mvme stored
// the module type in the crate config.
static ModuleTypeHints module_type_hints_from_mvme_config(const ListfileContext &ctx)
{
    auto sysEvent = ctx.readerHelper.preamble.findSystemEvent(mvlc::system_event::subtype::MVMEConfig);

    if (!sysEvent)
        return {};

    auto [vmeConfig, ec] = mvme::vme_config::read_vme_config_from_data(sysEvent->contents);

    if (!vmeConfig || ec)
    {
        spdlog::warn("could not read MVMEConfig from listfile: {}", ec.message());
        return {};
    }

    ModuleTypeHints hints;

    for (const auto &stack: ctx.crateConfig.stacks)
    {
        std::vector<std::string> stackHints;
        EventConfig *eventConfig = nullptr;

        for (auto ev: vmeConfig->getEventConfigs())
        {
            if (ev->objectName().toStdString() == stack.getName())
            {
                eventConfig = ev;
                break;
            }
        }

        for (const auto &group: stack.getGroups())
        {
            std::string typeName;

            if (eventConfig)
            {
                for (auto mod: eventConfig->getModuleConfigs())
                {
                    if (mod->isEnabled() && mod->objectName().toStdString() == group.name)
                    {
                        typeName = mod->getModuleMeta().typeName.toStdString();
                        break;
                    }
                }
            }

            stackHints.emplace_back(typeName);
        }

        hints.emplace_back(stackHints);
    }

    return hints;
}

static int cmd_explore(const ListfileContext &ctx, const ListfileModel &model)
{
    const auto &preamble = ctx.readerHelper.preamble;

    std::vector<std::string> sysEventNames;
    for (const auto &se: preamble.systemEvents)
        sysEventNames.emplace_back(mvlc::system_event_type_to_string(se.type));

    fmt::print("Listfile: {}\n", ctx.filename);
    fmt::print("  archive entries: {}\n", fmt::join(ctx.zipReader->entryNameList(), ", "));
    fmt::print("  listfile entry:  {}\n", ctx.entryName);
    fmt::print("  format:          {}\n",
               ctx.readerHelper.bufferFormat == mvlc::ConnectionType::USB ? "MVLC USB" : "MVLC ETH");
    fmt::print("  preamble:        {}\n", fmt::join(sysEventNames, ", "));
    fmt::print("  crate id:        {}\n", ctx.crateConfig.crateId);
    fmt::print("\n");

    std::vector<std::string> arrayLines;

    for (size_t ei = 0; ei < model.events.size(); ++ei)
    {
        const auto &event = model.events[ei];
        fmt::print("event[{}] {}\n", ei, event.name);

        for (size_t mi = 0; mi < event.modules.size(); ++mi)
        {
            const auto &mod = event.modules[mi];
            std::vector<std::string> metaStrings;
            for (const auto &[k, v]: mod.meta)
                metaStrings.emplace_back(fmt::format("{}={}", k, v));

            fmt::print("  module[{}] {}: type={}, {}{}\n", mi, mod.name, mod.typeName.empty() ? "?" : mod.typeName,
                       to_string(mod.match), mod.producesOutput ? "" : ", produces no output");

            if (!metaStrings.empty())
                fmt::print("    meta: {}\n", fmt::join(metaStrings, ", "));

            for (size_t ai = 0; ai < mod.arrays.size(); ++ai)
            {
                const auto &arr = mod.arrays[ai];
                fmt::print("    {:<48} size={:<4} filter={}\n", array_path(model, ei, mi, ai), arr.size,
                           arr.filterString);
            }
        }
    }

    return 0;
}

static std::vector<std::string> default_selectors(const ListfileModel &model)
{
    std::vector<std::string> result;

    for (size_t ei = 0; ei < model.events.size(); ++ei)
    {
        for (size_t mi = 0; mi < model.events[ei].modules.size(); ++mi)
        {
            if (!model.events[ei].modules[mi].arrays.empty())
                result.emplace_back(array_path(model, ei, mi, 0));
        }
    }

    if (result.size() != 1)
        return {};

    return result;
}

static void replay(ListfileContext &ctx, Exporter &exporter)
{
    auto event_data = [](void *userContext, int, int eventIndex, const mvlc::readout_parser::ModuleData *moduleDataList,
                         unsigned moduleCount)
    { reinterpret_cast<Exporter *>(userContext)->processEvent(eventIndex, moduleDataList, moduleCount); };

    auto system_event = [](void *, int, const u32 *, u32) {};

    mvlc::readout_parser::ReadoutParserCallbacks callbacks{event_data, system_event};
    mvlc::readout_parser::ReadoutParserCounters counters{};
    auto parser = mvlc::readout_parser::make_readout_parser(ctx.crateConfig.stacks, &exporter);

    const std::chrono::seconds ReportInterval(1);
    mvlc::util::Stopwatch sw;
    u32 bufferNumber = 1;

    auto report = [&]
    {
        auto s = sw.get_elapsed().count() / (1000.0 * 1000.0);
        auto MiB = ctx.readerHelper.totalBytesRead / (1024.0 * 1024.0);
        spdlog::info("read {:.2f} MiB, {} events, {} rows written, elapsed={:.2f} s, rate={:.2f} MiB/s", MiB,
                     exporter.counters().eventsSeen, exporter.counters().rowsWritten, s, MiB / s);
    };

    while (true)
    {
        ctx.readerHelper.destBuf().clear();
        auto buffer = mvlc::listfile::read_next_buffer(ctx.readerHelper);

        if (!buffer->used())
            break;

        auto view = buffer->viewU32();
        mvlc::readout_parser::parse_readout_buffer(ctx.readerHelper.bufferFormat, parser, callbacks, counters,
                                                   bufferNumber++, view.data(), view.size());

        if (sw.get_interval() >= ReportInterval)
        {
            report();
            sw.interval();
        }
    }

    report();
}

static std::string replace_extension(const std::string &path, const std::string &ext)
{
    QFileInfo fi(QString::fromStdString(path));
    return (fi.path() + "/" + fi.completeBaseName() + QString::fromStdString(ext)).toStdString();
}

static bool ends_with(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int cmd_export(ListfileContext &ctx, const ListfileModel &model, const argh::parser &parser,
                      std::vector<std::string> selectors)
{
    if (selectors.empty())
    {
        selectors = default_selectors(model);

        if (selectors.empty())
        {
            std::cerr << "Error: no arrays given and the listfile does not contain exactly one module with data.\n"
                      << "Use 'mvme_listfile_export explore " << ctx.filename << "' to list available arrays.\n";
            return 1;
        }

        spdlog::info("no arrays given, exporting {}", selectors.front());
    }

    auto selResult = resolve_selectors(model, selectors);

    if (!selResult.selection)
    {
        std::cerr << "Error: " << selResult.error << "\n";
        return 1;
    }

    std::string outFile = parser({"-o", "--output"}).str();
    std::string format = parser("--format").str();

    if (format.empty())
        format = (ends_with(outFile, ".txt") || ends_with(outFile, ".dat")) ? "text" : "csv";

    TextWriterOptions writerOpts;

    if (format == "csv")
        writerOpts = {",", "", true};
    else if (format == "text")
        writerOpts = {" ", "nan", true};
    else
    {
        std::cerr << "Error: unknown format '" << format << "'\n";
        return 1;
    }

    if (auto missing = parser("--missing").str(); !missing.empty())
    {
        if (missing == "nan")
            writerOpts.missing = "nan";
        else if (missing == "empty")
            writerOpts.missing = "";
        else if (missing == "0")
            writerOpts.missing = "0";
        else
        {
            std::cerr << "Error: --missing must be 'nan', 'empty' or '0'\n";
            return 1;
        }
    }

    if (auto sep = parser("--separator").str(); !sep.empty())
    {
        if (sep == "\\t" || sep == "tab")
            sep = "\t";
        writerOpts.separator = sep;
    }

    if (writerOpts.missing.empty() && (writerOpts.separator == " " || writerOpts.separator == "\t"))
        spdlog::warn("empty missing values with whitespace separated output will result in ambiguous rows");

    writerOpts.header = !parser["--no-header"];

    if (outFile.empty())
        outFile = replace_extension(ctx.filename, format == "csv" ? ".csv" : ".txt");

    const bool toStdout = outFile == "-";
    std::ofstream outFileStream;

    if (!toStdout)
    {
        outFileStream.open(outFile);

        if (!outFileStream)
        {
            std::cerr << "Error: could not open output file '" << outFile << "'\n";
            return 1;
        }
    }

    std::ostream &out = toStdout ? std::cout : outFileStream;
    const std::string outName = toStdout ? "stdout" : outFile;

    ExportOptions exportOpts;
    exportOpts.skipEmptyRows = parser["--skip-empty-rows"];

    TextRowWriter writer(out, writerOpts);
    Exporter exporter(model, *selResult.selection, writer, exportOpts);

    spdlog::info("writing {} columns to {}", selResult.selection->columnNames.size(), outName);

    exporter.begin();
    replay(ctx, exporter);
    exporter.end();

    const auto &c = exporter.counters();
    fmt::print(stderr, "Wrote {} rows to {}. Events: {}, events without data: {}{}\n", c.rowsWritten, outName,
               c.eventsSeen, c.emptyRows, exportOpts.skipEmptyRows && c.emptyRows ? " (skipped)" : "");

    if (c.emptyRows > 0)
        fmt::print(stderr, "Note: events without data usually indicate trigger problems.\n");

    if (c.overwrittenHits > 0)
        spdlog::warn("{} values were overwritten by a second hit on the same channel within one event. "
                     "Multi event readout is not supported yet, the output is likely incorrect.",
                     c.overwrittenHits);

    return 0;
}

static void qt_message_handler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    switch (type)
    {
    case QtDebugMsg:
    case QtInfoMsg:
        spdlog::debug("qt: {}", msg.toStdString());
        break;
    case QtWarningMsg:
        spdlog::warn("qt: {}", msg.toStdString());
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        spdlog::error("qt: {}", msg.toStdString());
        break;
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(qt_message_handler);
    QCoreApplication app(argc, argv);
    mvme_init("mvme_listfile_export", false);
    // mvlc loggers use stderr. Do the same for the spdlog default logger to keep stdout free for
    // exported data.
    auto defaultSinks = mvlc::get_default_sinks();
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("", std::begin(defaultSinks), std::end(defaultSinks)));
    mvlc::set_global_log_level(spdlog::level::warn);
    spdlog::set_level(spdlog::level::info);

    // argh treats a lone '-' as an option. Turn '-o -' into '--output=-' so stdout can be selected.
    std::vector<const char *> args(argv, argv + argc);

    for (size_t i = 1; i + 1 < args.size(); ++i)
    {
        if ((std::strcmp(args[i], "-o") == 0 || std::strcmp(args[i], "--output") == 0) &&
            std::strcmp(args[i + 1], "-") == 0)
        {
            args[i] = "--output=-";
            args.erase(args.begin() + i + 1);
        }
    }

    argh::parser parser({"-o", "--output", "--format", "--separator", "--missing", "--log-level"});
    parser.parse(static_cast<int>(args.size()), args.data());

    if (parser[{"-h", "--help"}] || parser.size() < 3)
    {
        std::cout << UsageText;
        return parser.size() < 3 && !parser[{"-h", "--help"}];
    }

    if (auto level = parser("--log-level").str(); !level.empty())
        spdlog::set_level(spdlog::level::from_str(level));

    const auto &pos = parser.pos_args();
    const auto command = pos[1];
    const auto filename = pos[2];
    std::vector<std::string> selectors(pos.begin() + 3, pos.end());

    if (command != "explore" && command != "export")
    {
        std::cerr << "Error: unknown command '" << command << "'\n\n" << UsageText;
        return 1;
    }

    std::unique_ptr<ListfileContext> ctx;

    try
    {
        ctx = open_listfile(filename);
    }
    catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error opening listfile {}: {}\n", filename, e.what());
        return 1;
    }

    auto filterDb = load_filter_db();
    auto model = make_listfile_model(ctx->crateConfig, filterDb, module_type_hints_from_mvme_config(*ctx));

    if (command == "explore")
        return cmd_explore(*ctx, model);

    try
    {
        return cmd_export(*ctx, model, parser, selectors);
    }
    catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error: {}\n", e.what());
        return 1;
    }
}
