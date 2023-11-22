#include <argh.h>
#include <iostream>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <mesytec-mvlc/util/fmt.h>

#include "multi_crate.h"
#include "util/qt_fs.h"
#include "template_system.h"
#include "vme_config.h"
#include "vme_config_util.h"

using namespace mesytec;
using namespace mesytec::mvme;

struct MulticrateTemplates
{
    std::unique_ptr<EventConfig> startEvent;
    std::unique_ptr<EventConfig> stopEvent;
    std::unique_ptr<EventConfig> masterEvent;
    std::unique_ptr<EventConfig> slaveEvent;
    QString setMasterModeScript;
    QString setSlaveModeScript;
    QString triggerIoScript;
};

MulticrateTemplates read_multicrate_templates()
{
    MulticrateTemplates result;
    auto dir = QDir(vats::get_template_path());

    result.startEvent  = vme_config::eventconfig_from_file(dir.filePath("multicrate/start_event.mvmeevent"));
    result.stopEvent   = vme_config::eventconfig_from_file(dir.filePath("multicrate/stop_event.mvmeevent"));
    result.masterEvent = vme_config::eventconfig_from_file(dir.filePath("multicrate/master_event0.mvmeevent"));
    result.slaveEvent  = vme_config::eventconfig_from_file(dir.filePath("multicrate/slave_event0.mvmeevent"));

    result.setMasterModeScript = read_text_file(dir.filePath("multicrate/set_master_mode.vmescript"));
    result.setSlaveModeScript  = read_text_file(dir.filePath("multicrate/set_slave_mode.vmescript"));
    result.triggerIoScript     = read_text_file(dir.filePath("multicrate/mvlc_trigger_io.vmescript"));

    return result;
}

std::vector<std::unique_ptr<VMEConfig>> make_multicrate_vme_configs(const std::vector<std::string> &mvlcUrls)
{
    // read template files from multicrate subdirectory
    // create vme config for each mvlc url
    // setup the mvlc for each vme config
    // create start and stop events from the event templates

    if (mvlcUrls.empty())
        return {};

    std::vector<std::unique_ptr<VMEConfig>> result;
    auto templates = read_multicrate_templates();

    for (auto urlIter = std::begin(mvlcUrls); urlIter != std::end(mvlcUrls); ++urlIter)
    {
        auto controllerInfo = vme_config::mvlc_settings_from_url(*urlIter);
        auto name = fmt::format("crate{}", std::distance(std::begin(mvlcUrls), urlIter));

        auto vmeConfig = std::make_unique<VMEConfig>();
        vmeConfig->setObjectName(name.c_str());
        vmeConfig->setVMEController(controllerInfo.first, controllerInfo.second);
        vmeConfig->addEventConfig(vme_config::clone_config_object(*templates.startEvent).release());
        vmeConfig->addEventConfig(vme_config::clone_config_object(*templates.stopEvent).release());
        auto setMasterSlaveScript = std::make_unique<VMEScriptConfig>();

        if (urlIter == std::begin(mvlcUrls))
        {
            vmeConfig->addEventConfig(vme_config::clone_config_object(*templates.masterEvent).release());
            setMasterSlaveScript->setObjectName("set master mode");
            setMasterSlaveScript->setScriptContents(templates.setMasterModeScript);
        }
        else
        {
            vmeConfig->addEventConfig(vme_config::clone_config_object(*templates.slaveEvent).release());
            setMasterSlaveScript->setObjectName("set slave mode");
            setMasterSlaveScript->setScriptContents(templates.setSlaveModeScript);
        }

        vmeConfig->addGlobalScript(setMasterSlaveScript.release(), "daq_start");

        if (auto triggerIo = vmeConfig->getMVLCTriggerIOScript())
            triggerIo->setScriptContents(templates.triggerIoScript);

        result.emplace_back(std::move(vmeConfig));
    }

    return result;
}

int create_configs(const std::vector<std::string> &mvlcUrls, const std::string &outputDirectory)
{
    if (mvlcUrls.empty())
        return 1;

    auto vmeConfigs = make_multicrate_vme_configs(mvlcUrls);

    QDir().mkdir(outputDirectory.c_str());
    QDir outDir(outputDirectory.c_str());

    for (auto &vmeConfig: vmeConfigs)
    {
        QFile out(outDir.filePath(vmeConfig->objectName() + QSL(".vme")));

        if (!out.open(QIODevice::WriteOnly))
        {
            std::cerr << fmt::format("Error opening output file {} for writing: {}\n",
                out.fileName().toLocal8Bit().data(), out.errorString().toLocal8Bit().data());
            return 1;
        }

        vme_config::serialize_vme_config_to_device(out, *vmeConfig);
    }

    multi_crate::MulticrateVMEConfig combinedConfig;
    combinedConfig.setObjectName("combined");

    for (auto &vmeConfig: vmeConfigs)
    {
        combinedConfig.addCrateConfig(vmeConfig.release());
    }

    vmeConfigs.clear();

    {
        QFile out(outDir.filePath(combinedConfig.objectName() + QSL(".multicratevme")));

        if (!out.open(QIODevice::WriteOnly))
        {
            std::cerr << fmt::format("Error opening output file {} for writing: {}\n",
                out.fileName().toLocal8Bit().data(), out.errorString().toLocal8Bit().data());
            return 1;
        }

        vme_config::serialize_multicrate_config_to_device(out, combinedConfig);
    }

    return 0;
}

struct ReplayTestContext
{
    size_t totalBuffersSeen;
    size_t totalBytesSeen;
};

// This tests listfile reading without knowing the file type. It works assuming
// that USB/system frames do not collide with eth headers.
void replay_test_consume_buffer(ReplayTestContext &ctx, const mvlc::ReadoutBuffer *buffer)
{
    ++ctx.totalBuffersSeen;
    ctx.totalBytesSeen += buffer->used();
    auto input = buffer->viewU32();

    auto it = std::begin(input);

    while (it < std::end(input))
    {
        u32 header = *it;

        std::cout << fmt::format("header=0x{:08X}: ", header);

        if (mvlc::is_known_frame_header(header))
        {
            auto frameInfo = mvlc::extract_frame_info(header);
            std::cout << fmt::format("Found known frame header 0x{:08X}, len={}, crate={}",
                header, frameInfo.len, frameInfo.ctrl);
            if (frameInfo.type == mvlc::frame_headers::SystemEvent)
                std::cout << ", sysevent=" << mvlc::system_event_type_to_string(frameInfo.sysEventSubType);
            std::cout << "\n";
            it += 1 + frameInfo.len;
        }
        else if (it + 1 < std::end(input))
        {
            u32 header1 = *(it+1);
            mvlc::eth::PayloadHeaderInfo ethInfo{ header, header1 };
            std::cout << fmt::format("Assuming eth packet: packetNumber={}, len={}, crate={}\n",
                ethInfo.packetNumber(), ethInfo.dataWordCount(), ethInfo.controllerId());
            it += 2 + ethInfo.dataWordCount();
        }
        else
        {
            throw std::runtime_error(fmt::format("Landed on unknown data word 0x{:08X}\n", header));
        }
    }

    std::cout << "End of buffer reached\n";
}

void do_replay_test(const std::string &filename)
{
    mvlc::listfile::SplitZipReader reader;
    reader.openArchive(filename);
    auto readHandle = reader.openFirstListfileEntry();
    auto preamble = mvlc::listfile::read_preamble(*readHandle);
    std::cout << fmt::format("input file {}: magic={}\n", filename, preamble.magic);

    mvlc::ReadoutBufferQueues bufferQueues;
    mvlc::ReplayWorker replayWorker(bufferQueues, readHandle);

    if (auto ec = replayWorker.start().get())
    {
        std::cerr << "Error from replay worker: " << ec.message() << "\n";
        return;
    }

    ReplayTestContext ctx{};

    while (true)
    {
        auto buffer = bufferQueues.filledBufferQueue().dequeue(std::chrono::milliseconds(500));

        if (!buffer)
        {
            std::cerr << "Did not get a buffer from the buffer queue, leaving loop\n";
            break;
        }

        replay_test_consume_buffer(ctx, buffer);
        bufferQueues.emptyBufferQueue().enqueue(buffer);
    }

    if (replayWorker.state() != mvlc::ReplayWorker::State::Idle)
        if (auto ec = replayWorker.stop())
            std::cerr << "Warning: error from replay worker: " << ec.message() << "\n";

    return;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    argh::parser parser({"-h", "--help", "--log-level"});
    parser.parse(argv);

    if (parser[{"-h", "--help"}])
    {
        std::cout << "subcommands: create-configs, replay-test\n";
        std::cout << "RTFS for details :p\n";
    }

    auto cmdName = parser[1];

    if (cmdName.empty()) return 0;

    if (cmdName == "create-configs")
    {
        parser.add_params({"--mvlc", "-O", "--output-directory"});
        parser.parse(argv);

        std::vector<std::string> mvlcUrls;
        for (const auto &url: parser.params("mvlc"))
            mvlcUrls.emplace_back(url.second);

        if (mvlcUrls.empty())
        {
            std::cerr << "Must specify at least one MVLC URL (--mvlc).\n";
            return 1;
        }

        std::string outputDirectory;

        if (!(parser({"-O", "--output-directory"}) >> outputDirectory))
        {
            std::cerr << "Must provide an output directory (-O|--output-directory).\n";
            return 1;
        }

        return create_configs(mvlcUrls, outputDirectory);
    }
    else if (cmdName == "replay-test")
    {
        auto inputFilename = parser[2];
        if (inputFilename.empty())
        {
            std::cerr << "Error: missing input filename\n";
            return 1;
        }

        std::cout << "input file is " << inputFilename << "\n";
        do_replay_test(inputFilename);
    }

    return 0;
}
