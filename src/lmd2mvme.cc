// #include "mvme_session.h"
#include <argh.h>
#include <filesystem>
#include <iostream>
#include <spdlog/spdlog.h>
#include <QCoreApplication>

#include "mbs_wrappers.h"
#include "mvlc_daq.h"
#include "mvlc/vmeconfig_to_crateconfig.h"
#include "mvme_mvlc_listfile.h"
#include "mvme_session.h"
#include "vme_config.h"

using namespace mesytec::mvme::mbs;
using namespace mesytec;

int main(int argc, char *argv[])
{
    mvme_init("lmd2mvme", false);
    QCoreApplication theQApp(argc, argv);
    spdlog::set_level(spdlog::level::info);

    fmt::print("Struct sizes: s_ve10_1={}, s_ves10_1={}, s_bufhe={}\n", sizeof(s_ve10_1),
               sizeof(s_ves10_1), sizeof(s_bufhe));

    argh::parser parser;
    parser.parse(argc, argv,
                 argh::parser::PREFER_FLAG_FOR_UNREG_OPTION |
                     argh::parser::SINGLE_DASH_IS_MULTIFLAG);
    ;
    auto optOverwrite = parser.flags().count("f");
    bool optPrintData = parser["--print-data"];

    if (parser.pos_args().size() < 3)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <input-file.lmd> <mvme-vme-config.vme> [mvme-output-file.zip]" << std::endl;
        return 1;
    }

    auto lmdFilename = parser.pos_args().at(1);
    auto mvmeVmeConfigFilename = parser.pos_args().at(2);
    std::string outputFilename;
    if (parser.pos_args().size() > 3)
        outputFilename = parser.pos_args().at(3);

    if (outputFilename.empty() || outputFilename.length() == 0)
        outputFilename =
            std::filesystem::path(mvmeVmeConfigFilename).filename().stem().string() + ".zip";

    if (!optOverwrite && std::filesystem::exists(outputFilename))
    {
        std::cerr << "Output file already exists: " << outputFilename << "\n";
        return 1;
    }

    // ########## Read the mvme VME config file and perform some checks.
    spdlog::info("Reading and checking mvme VME config...");
    auto [vmeConfig, error] = read_vme_config_from_file(mvmeVmeConfigFilename.c_str());
    if (!vmeConfig)
    {
        spdlog::error("Error reading VME config file {}: {}", mvmeVmeConfigFilename,
                      error.toLocal8Bit().constData());
    }

    auto mvlcCrateConfig = mvme::vmeconfig_to_crateconfig(vmeConfig.get());

    // Events in the base GSI config are: dispatch, start_acq, stop_acq, trigger1, trigger2, trigger3, trigger4
    // Expected event irq trigger values. The array is indexed by one-based readout_stack/event number.
    static const std::vector<int> expectedIrqTriggers = {4, 14, 15, 8, 9, 10, 11};
    static const std::vector<int> mbsTriggerToStackNum =
    {
         -1, // 0 is not a valid mbs trigger number
          3, // trigger 1 is read out using stack 3
          4, // trigger 2 is not present in the MUSIC CaveC run
          3, // trigger 3 should be read out by stack 5 but it's likely from a TRIVA bug where multiple bits are or'red to form the trigger value or similar. => map it to 3 too
          6, // trigger 4 is not present in the MUSIC CaveC run
    };
    static const int outputCrateIndex = 0;
    static const int dispatchEventIndex = 0;

    if (mvlcCrateConfig.stacks.size() != expectedIrqTriggers.size() ||
        mvlcCrateConfig.triggers.size() != expectedIrqTriggers.size())
    {
        spdlog::error("Structural error: Expected to find {} IRQ triggered readout stacks in VME "
                      "config, found {}.",
                      expectedIrqTriggers.size(), mvlcCrateConfig.stacks.size());
        return 1;
    }

    for (size_t stackIndex = 0; stackIndex < expectedIrqTriggers.size(); ++stackIndex)
    {
        mvlc::stacks::Trigger trigger;
        trigger.value = mvlcCrateConfig.triggers[stackIndex];
        if (trigger.type != mvlc::stacks::TriggerType::IRQWithIACK &&
            trigger.type != mvlc::stacks::TriggerType::IRQNoIACK)
        {
            spdlog::error("Structural error: Expected IRQ trigger type, found {}.",
                mvlc::trigger_to_string(trigger));

            return 1;
        }

        auto irqValue = mvlc::get_trigger_irq_value(trigger);
        assert(irqValue.has_value());

        if (irqValue.value() != expectedIrqTriggers[stackIndex])
        {
            spdlog::error("Structural error: Expected IRQ{} to trigger event{} but got IRQ{}",
                expectedIrqTriggers[stackIndex], stackIndex, irqValue.value());
            return 1;
        }
    }

    auto rdoStructure = mvlc::readout_parser::build_readout_structure(mvlcCrateConfig.stacks);
    // How many words are read out by the modules in the dispatch stacks. This
    // is the number of words to remove from the front of each MBS subevent and
    // put into the generated output frame for the dispatch event.
    size_t dispatchReadoutDataWords = 0;
    try
    {
        auto dispatchEventName = mvlcCrateConfig.stacks.at(0).getName();
        for (auto &moduleReadout: rdoStructure.at(0))
        {
            if (moduleReadout.hasDynamic)
            {
                spdlog::error("Structural error: the dispatch event {} contains a VME block read command",
                    dispatchEventName);
                return 1;
            }

            dispatchReadoutDataWords += moduleReadout.prefixLen + moduleReadout.suffixLen;
        }

        // The first data word from the dispatch event (0x12..78) marker is not
        // present in the mbs subevent readout data. Is probably is used and
        // swallowed by the m_readout_mvlc program.
        //spdlog::info("The dispatch event {} produces {} words of data, adjusting to {} to account for the missing marker word.",
        //     dispatchEventName, dispatchReadoutDataWords, dispatchReadoutDataWords-1);
        spdlog::info("Size of the TRIVA dispatch readout: {} words", dispatchReadoutDataWords);
    }
    catch(const std::exception& e)
    {
        spdlog::error("Unexpected readout structure in input VME config: {}", e.what());
        return 1;
    }

    // This many words from the dispatch stack are present in the mbs subevent
    // data. The rest must have been swallowed by the m_readout_mvlc program.
    static const size_t dispatchDataWordsInMbsEvent = 5;

    // ########## Quick scan of the LMD file
    spdlog::info("Scanning LMD file...");
    mvlc::util::Stopwatch swQuickScan;
    auto scanResult = scan_lmd_file(lmdFilename);
    auto dtQuickScan = swQuickScan.interval();
    auto lmdFilesize = std::filesystem::file_size(lmdFilename);
    auto scanRate =
        (lmdFilesize / (1024.0 * 1024)) /
        (std::chrono::duration_cast<std::chrono::milliseconds>(dtQuickScan).count() / 1000);
    spdlog::info("Scan of LMD file {} took {} ms, rate={} MB/s", lmdFilename,
                 std::chrono::duration_cast<std::chrono::milliseconds>(dtQuickScan).count(),
                 scanRate);
    spdlog::info("Scan result: {}", scanResult);

    if (scanResult.subcrates.size() == 0)
    {
        spdlog::error("No subcrates found in LMD file. Exiting.");
        return 1;
    }

    if (scanResult.subcrates.size() > 1)
    {
        spdlog::error("Found {} subcrates in LMD file, expected only one subcrate.",
                      scanResult.subcrates.size());
        return 1;
    }

    // Do some work
    // Do some more work
    // Take a break from all the work
    // Coffee
    // Bliss

    LmdWrapper lmdWrapper;
    lmdWrapper.open(lmdFilename, false);

    spdlog::info("Processing LMD file: {}. file header: {}", lmdFilename, *lmdWrapper.fileHeaderPtr);

    auto make_listfile_preamble = [vmeConfig=vmeConfig.get()]() -> std::vector<u8>
    {
        using namespace mvlc;
        listfile::BufferedWriteHandle bwh;
        listfile::listfile_write_magic(bwh, ConnectionType::USB);
        auto crateConfig = mesytec::mvme::vmeconfig_to_crateconfig(vmeConfig);
        // Removes non-output-producing command groups from each of the readout
        // stacks. Mirrors the setup done in MVLC_StreamWorker::start().
        //crateConfig.stacks = mvme_mvlc::sanitize_readout_stacks(crateConfig.stacks);
        listfile::listfile_write_endian_marker(bwh, crateConfig.crateId);
        listfile::listfile_write_crate_config(bwh, crateConfig);
        static const u8 crateId = 0; // FIXME: single crate only!
        mvme_mvlc::listfile_write_mvme_config(bwh, crateId, *vmeConfig);
        return bwh.getBuffer();
    };

    ListFileOutputInfo lfOutInfo;
    lfOutInfo.format = ListFileFormat::ZIP;
    auto lfSetup = mvme_mvlc::make_listfile_setup(lfOutInfo, make_listfile_preamble());
    lfSetup.filenamePrefix = std::filesystem::path(outputFilename).filename().stem().string();
    auto zipCreator = std::make_unique<mvlc::listfile::SplitZipCreator>();
    std::unique_ptr<mvlc::listfile::WriteHandle> lfWriteHandle = nullptr;
    try
    {
        zipCreator->createArchive(lfSetup);
        lfWriteHandle = zipCreator->createListfileEntry();
    }
    catch (const std::exception &e)
    {
        spdlog::error("Error creating output listfile {}: {}", outputFilename, e.what());
        return 1;
    }

    mvlc::ReadoutBuffer outputBuffer(mvlc::util::Megabytes(1));
    std::vector<u32> tmpBuffer;

    auto maybe_flush_buffer = [&]
    {
        if (const auto used = outputBuffer.used(); used > mvlc::util::Megabytes(1) - 256)
        {
            lfWriteHandle->write(outputBuffer.data(), used);
            outputBuffer.clear();
        }
    };


    size_t inputEventCount = 0;
    size_t outputDispatchEvents = 0;
    size_t outputDataEvents = 0;

    // Walk trough the input file and for each mbs subevent:
    // - Generate an output event for the dispatch event. Copy the first
    //   dispatchReadoutDataWords words to this event.
    // - Generate an output event for the actual trigger stack. The stack index is based on the input events i_trigger value.
    //   Copy the rest of the data into this event.
    // - Done with the subevent, go to the next one.

    while (lmdWrapper.nextEvent())
    {
        //spdlog::info("  Event{}: {}", eventCount, *lmdWrapper.eventPtr);
        if (lmdWrapper.subEventCount() == 0)
        {
            spdlog::warn("Event {} has 0 subevents.", inputEventCount);
            continue;
        }

        if (lmdWrapper.subEventCount() > 1)
        {
            spdlog::warn("Event {} has {} subevents, only handling the first one!", inputEventCount,
                         lmdWrapper.subEventCount());
        }

        if (!lmdWrapper.nextSubEvent())
            throw std::runtime_error("error getting first subevent");

        //spdlog::info("    SubEvent: {}", *lmdWrapper.subEventPtr);

        auto subEventView = lmdWrapper.subEventView();
        if (optPrintData)
            fmt::print("Subevent ({} words, {} bytes): {:#010x}\n", subEventView.size(),
                    subEventView.size() * sizeof(u32), fmt::join(subEventView, ", "));

        if (subEventView.size() < dispatchReadoutDataWords)
            spdlog::warn("Subevent data too short, skipping event.");

        // add fake data for stuff that was swallowed by mbs

        auto fillerWordCount = dispatchReadoutDataWords - dispatchDataWordsInMbsEvent;
        auto wordsToCopy = std::min(subEventView.size(), dispatchDataWordsInMbsEvent);
        tmpBuffer.resize(fillerWordCount);
        std::fill(tmpBuffer.begin(), tmpBuffer.end(), 0xdeadbeef);
        std::copy(std::begin(subEventView), std::begin(subEventView) + wordsToCopy, std::back_inserter(tmpBuffer));
        subEventView.remove_prefix(wordsToCopy);

        mvlc::listfile::write_event_data(outputBuffer, outputCrateIndex, dispatchEventIndex, tmpBuffer.data(), tmpBuffer.size());
        ++outputDispatchEvents;

        auto mbsTrigger = lmdWrapper.eventPtr->i_trigger;

        if (!subEventView.empty() && 1 <= mbsTrigger && static_cast<size_t>(mbsTrigger) < mbsTriggerToStackNum.size())
        {
            auto outputEventIndex = mbsTriggerToStackNum[lmdWrapper.eventPtr->i_trigger];
            mvlc::listfile::write_event_data(outputBuffer, 0, outputEventIndex, subEventView.data(), subEventView.size());
            ++outputDataEvents;
        }
        else
            spdlog::warn("Not writing output readout event for mbs trigger {}", mbsTrigger);

        maybe_flush_buffer();
        ++inputEventCount;
    }

    maybe_flush_buffer();
    auto totalOutputEvents = outputDispatchEvents + outputDataEvents;
    spdlog::info("Done processing LMD file: inputEventCount={}, outputDispatchEvents={}, outputDataEvents={}, outputTotalEvents={}",
         inputEventCount, outputDispatchEvents, outputDataEvents, totalOutputEvents);

    return 0;
}
