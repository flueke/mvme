// Replays an MVLC listfile and runs the mdpp sample decoder on the data of each module with a
// supported module type. Reports events for which the decoder logged warnings or errors, together
// with the decoder log and the raw module data.

#include <argh.h>
#include <iostream>
#include <QCoreApplication>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <spdlog/spdlog.h>

#include "mdpp-sampling/mdpp_decode.h"
#include "mvme_session.h"
#include "vme_config.h"
#include "vme_config_util.h"

using namespace mesytec;
namespace mdpp_sampling = mesytec::mvme::mdpp_sampling;

static const char *UsageText = R"~(usage:
  dev_mdpp_sample_decoder <listfile.zip> [options]

Replays the listfile and runs the mdpp sample decoder on the data of each module with a supported
type (mdpp16_scp, mdpp16_qdc, mdpp32_scp, mdpp32_qdc). By default only events for which the decoder
logged a warning or an error are reported.

Options:
  --event <index>         Only decode modules of this event (readout stack) index.
  --module <index>        Only decode the module with this index. Requires --event.
  --type <type>           Decoder module type to use instead of the type from the listfile.
                          Requires --event and --module.
  --all                   Report all decoded events, not only those with warnings or errors.
  --trace                 Include the decoders trace messages in the reported logs.
  --max-reports <n>       Stop after reporting n events.
  --log-level <level>     One of trace, debug, info, warn, error, off.
  -h, --help              Show this help.
)~";

struct ListfileContext
{
    std::string filename;
    std::unique_ptr<mvlc::listfile::ZipReader> zipReader;
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
    auto entryName = ctx->zipReader->firstListfileEntryName();

    if (entryName.empty())
        throw std::runtime_error("no listfile entry found in archive");

    ctx->readHandle = ctx->zipReader->openEntry(entryName);
    ctx->readerHelper = mvlc::listfile::make_listfile_reader_helper(ctx->readHandle);

    auto crateConfigEvent = ctx->readerHelper.preamble.findCrateConfig();

    if (!crateConfigEvent)
        throw std::runtime_error("no MVLC crate config found in listfile (not an MVLC listfile?)");

    ctx->crateConfig = mvlc::crate_config_from_yaml(crateConfigEvent->contentsToString());

    return ctx;
}

struct ModuleInfo
{
    std::string name;
    std::string typeName;
    mdpp_sampling::DecoderFunction decoder;

    size_t eventsDecoded = 0;
    size_t eventsWithWarnings = 0;
    size_t eventsWithErrors = 0;
    size_t tracesDecoded = 0;
};

// Module names and types indexed by [eventIndex][moduleIndex]. Types come from the crate config
// meta info if present, otherwise from the MVMEConfig stored in the listfile preamble.
static std::vector<std::vector<ModuleInfo>> make_module_infos(const ListfileContext &ctx)
{
    std::unique_ptr<VMEConfig> vmeConfig;

    if (auto sysEvent = ctx.readerHelper.preamble.findSystemEvent(mvlc::system_event::subtype::MVMEConfig))
    {
        std::error_code ec;
        std::tie(vmeConfig, ec) = mvme::vme_config::read_vme_config_from_data(sysEvent->contents);

        if (ec)
            spdlog::warn("could not read MVMEConfig from listfile: {}", ec.message());
    }

    std::vector<std::vector<ModuleInfo>> result;

    for (const auto &stack: ctx.crateConfig.stacks)
    {
        EventConfig *eventConfig = nullptr;

        if (vmeConfig)
        {
            for (auto ev: vmeConfig->getEventConfigs())
            {
                if (ev->objectName().toStdString() == stack.getName())
                {
                    eventConfig = ev;
                    break;
                }
            }
        }

        std::vector<ModuleInfo> modules;

        for (const auto &group: stack.getGroups())
        {
            ModuleInfo mi;
            mi.name = group.name;

            if (auto it = group.meta.find("vme_module_type"); it != group.meta.end())
            {
                mi.typeName = it->second;
            }
            else if (eventConfig)
            {
                for (auto mod: eventConfig->getModuleConfigs())
                {
                    if (mod->isEnabled() && mod->objectName().toStdString() == group.name)
                    {
                        mi.typeName = mod->getModuleMeta().typeName.toStdString();
                        break;
                    }
                }
            }

            modules.emplace_back(std::move(mi));
        }

        result.emplace_back(std::move(modules));
    }

    return result;
}

struct ReplayOptions
{
    bool reportAll = false;
    bool includeTrace = false;
    size_t maxReports = 0; // 0: unlimited
};

struct ReplayContext
{
    std::vector<std::vector<ModuleInfo>> modules;
    ReplayOptions opts;
    u32 bufferNumber = 0;
    size_t eventNumber = 0;
    size_t reportsWritten = 0;
};

static void process_event(ReplayContext &ctx, int eventIndex, const mvlc::readout_parser::ModuleData *moduleDataList,
                          unsigned moduleCount)
{
    ++ctx.eventNumber;

    if (eventIndex < 0 || static_cast<size_t>(eventIndex) >= ctx.modules.size())
        return;

    auto &eventModules = ctx.modules[eventIndex];

    for (unsigned mi = 0; mi < std::min(moduleCount, static_cast<unsigned>(eventModules.size())); ++mi)
    {
        auto &module = eventModules[mi];
        const auto &data = moduleDataList[mi].data;

        // Same condition as in Analysis::processModuleData().
        if (!module.decoder || !data.size)
            continue;

        std::vector<std::string> logLines;
        bool sawWarning = false;
        bool sawError = false;

        auto logger = [&](const std::string &level, const std::string &message)
        {
            if (level == "warn")
                sawWarning = true;
            else if (level == "error")
                sawError = true;

            if (level != "trace" || ctx.opts.includeTrace)
                logLines.emplace_back(fmt::format("[{}] {}", level, message));
        };

        auto decoded = module.decoder(data.data, data.size, logger);

        ++module.eventsDecoded;
        module.tracesDecoded += decoded.traces.size();
        if (sawWarning)
            ++module.eventsWithWarnings;
        if (sawError)
            ++module.eventsWithErrors;

        if (!(sawWarning || sawError || ctx.opts.reportAll))
            continue;

        if (ctx.opts.maxReports && ctx.reportsWritten >= ctx.opts.maxReports)
            continue;

        ++ctx.reportsWritten;

        fmt::print("=== event #{} (buffer #{}), event[{}] module[{}] {} ({}): {}, size={}, #traces={}\n",
                   ctx.eventNumber, ctx.bufferNumber, eventIndex, mi, module.name, module.typeName,
                   sawError ? "error" : (sawWarning ? "warning" : "ok"), data.size, decoded.traces.size());

        for (const auto &line: logLines)
            fmt::print("  {}\n", line);

        fmt::print("  input data:\n");

        for (u32 i = 0; i < data.size; ++i)
            fmt::print("  {:4d}: {:#010x}\n", i, data.data[i]);

        fmt::print("\n");
    }
}

static void replay(ListfileContext &lfCtx, ReplayContext &ctx)
{
    auto event_data = [](void *userContext, int, int eventIndex, const mvlc::readout_parser::ModuleData *moduleDataList,
                         unsigned moduleCount)
    { process_event(*reinterpret_cast<ReplayContext *>(userContext), eventIndex, moduleDataList, moduleCount); };

    auto system_event = [](void *, int, const u32 *, u32) {};

    mvlc::readout_parser::ReadoutParserCallbacks callbacks{event_data, system_event};
    mvlc::readout_parser::ReadoutParserCounters counters{};
    auto parser = mvlc::readout_parser::make_readout_parser(lfCtx.crateConfig.stacks, &ctx);

    while (!ctx.opts.maxReports || ctx.reportsWritten < ctx.opts.maxReports)
    {
        lfCtx.readerHelper.destBuf().clear();
        auto buffer = mvlc::listfile::read_next_buffer(lfCtx.readerHelper);

        if (!buffer->used())
            break;

        auto view = buffer->viewU32();
        mvlc::readout_parser::parse_readout_buffer(lfCtx.readerHelper.bufferFormat, parser, callbacks, counters,
                                                   ++ctx.bufferNumber, view.data(), view.size());
    }
}

static int parse_index(const argh::parser &parser, const char *name)
{
    int result = -1;
    parser(name, -1) >> result;
    return result;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    mvme_init("dev_mdpp_sample_decoder", false);
    auto defaultSinks = mvlc::get_default_sinks();
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("", std::begin(defaultSinks), std::end(defaultSinks)));
    mvlc::set_global_log_level(spdlog::level::warn);
    spdlog::set_level(spdlog::level::info);

    argh::parser parser({"--event", "--module", "--type", "--max-reports", "--log-level"});
    parser.parse(argc, argv);

    if (parser[{"-h", "--help"}] || parser.pos_args().size() != 2)
    {
        std::cout << UsageText;
        return parser.pos_args().size() != 2 && !parser[{"-h", "--help"}];
    }

    if (auto level = parser("--log-level").str(); !level.empty())
        spdlog::set_level(spdlog::level::from_str(level));

    const auto filename = parser.pos_args()[1];
    const int selectedEvent = parse_index(parser, "--event");
    const int selectedModule = parse_index(parser, "--module");
    const auto typeOverride = parser("--type").str();

    if (selectedModule >= 0 && selectedEvent < 0)
    {
        std::cerr << "Error: --module requires --event\n";
        return 1;
    }

    if (!typeOverride.empty() && selectedModule < 0)
    {
        std::cerr << "Error: --type requires --event and --module\n";
        return 1;
    }

    std::unique_ptr<ListfileContext> lfCtx;

    try
    {
        lfCtx = open_listfile(filename);
    }
    catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error opening listfile {}: {}\n", filename, e.what());
        return 1;
    }

    ReplayContext ctx;
    ctx.modules = make_module_infos(*lfCtx);
    ctx.opts.reportAll = parser["--all"];
    ctx.opts.includeTrace = parser["--trace"];
    parser("--max-reports", 0) >> ctx.opts.maxReports;

    if (selectedEvent >= static_cast<int>(ctx.modules.size()) ||
        (selectedModule >= 0 && selectedModule >= static_cast<int>(ctx.modules[selectedEvent].size())))
    {
        std::cerr << "Error: event or module index out of range\n";
        return 1;
    }

    if (!typeOverride.empty())
        ctx.modules[selectedEvent][selectedModule].typeName = typeOverride;

    size_t decoderCount = 0;

    for (size_t ei = 0; ei < ctx.modules.size(); ++ei)
    {
        for (size_t mi = 0; mi < ctx.modules[ei].size(); ++mi)
        {
            auto &module = ctx.modules[ei][mi];
            const bool selected = (selectedEvent < 0 || selectedEvent == static_cast<int>(ei)) &&
                                  (selectedModule < 0 || selectedModule == static_cast<int>(mi));

            if (selected)
                module.decoder = mdpp_sampling::get_decoder_function(module.typeName.c_str());

            if (module.decoder)
                ++decoderCount;

            spdlog::info("event[{}] module[{}] {}: type={}{}", ei, mi, module.name,
                         module.typeName.empty() ? "?" : module.typeName, module.decoder ? ", decoding" : "");
        }
    }

    if (!decoderCount)
    {
        std::cerr << "Error: no modules with a supported decoder type selected. Use --type to override.\n";
        return 1;
    }

    try
    {
        replay(*lfCtx, ctx);
    }
    catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error: {}\n", e.what());
        return 1;
    }

    fmt::print("Summary: {} events, {} reported\n", ctx.eventNumber, ctx.reportsWritten);

    for (size_t ei = 0; ei < ctx.modules.size(); ++ei)
    {
        for (size_t mi = 0; mi < ctx.modules[ei].size(); ++mi)
        {
            const auto &m = ctx.modules[ei][mi];

            if (m.decoder)
                fmt::print("  event[{}] module[{}] {} ({}): decoded={}, with warnings={}, with errors={}, traces={}\n",
                           ei, mi, m.name, m.typeName, m.eventsDecoded, m.eventsWithWarnings, m.eventsWithErrors,
                           m.tracesDecoded);
        }
    }

    return 0;
}
