/*

Purpose of the mvme_listfile_reader program:

- Read mvme listfiles and pass readout data to handler code.
- Accept any type of mvme listfile types (MVMELST, MVLC_*) both as a flat file
  and within a ZIP archive.
- If multi event splitting is wanted load the filter strings from the standard
  mvme template system (vats).
- Load user specified code and invoke it with data extracted from the listfile.

mvme_listfile_reader
    input_listfile_filename0, input_listfile_filename1, ...

    --multi-event-splitting / --no-multi-event-splitting
    --dump-data / --no-dump-data
    --plugin my_c_plugin.so,--foo=bar,-osomething
    --plugin my_qt_plugin.so

Notes:
- There is no need to specify the library extension when using
  QPluginLoader or QLibrary.

- C-style libraries need to be wrapped in an extern "C" block if compiled using
  a c++ compiler.

- make the dumper/printer code a plugin too and prepend it to the list of
  default plugins. Remove it if --no-print-data is specified.

- multi event splitting config is stored in the analysis right now. This
  functionality should be included in the mvme-read-listfile program.

  The problem is: need to get the filter string for multi event splitting for
  each module. This depends on the module type. If this was stored in the vme
  config at module creation time it would be easy to get.
  The other way to get the split filter is to use the template system and load
  the strings from there.
  Just use the template system for now but think about implementing the change
  to store the filter strings (and possibly all module meta) directly inside
  the vme config.

  Another concern: the multi event splitter could be a plugin in itself. This
  plugin would produce output just like the coding reading and parsing the
  listfile but it would yield N output events for each incoming event.
  Should plugins be given the possibilty to call output functions? This would
  be a bit more complicated than just consuming the data as a correct array of
  ModuleData structures would have to be filled.

  -> This looks more like GO4 than something small and compact and quick to
  use. Probably these things are outside the scope of this tool at least for
  the first ieration.

- How to handle system events? These do not really fit into the event.module
  scheme.

*/

#include <iostream>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QLibrary>
#include <QThread>

#include "analysis/analysis.h"
#include "analysis/a2/memory.h"
#include "listfile_reader/listfile_reader.h"
#include "listfile_replay.h"
#include "mvlc/mvlc_readout_parsers.h"
#include "mvme_listfile_utils.h"
#include "mvlc_stream_worker.h" // FIXME: move collect_readout_scripts() elsewhere (a general readout parser file)
#include "vme_controller_factory.h"
#include "vme_config_scripts.h"

using std::cout;
using std::endl;

struct RawDataPlugin
{
    QString filename;
    PluginInfo info;
    PluginInit init;
    PluginDestroy destroy;
    BeginRun begin_run;
    EventData event_data;
    EndRun end_run;
    void *userptr;
};

struct library_load_error: public std::exception {};
struct resolve_error: public std::exception {};

template<typename Signature>
Signature resolve(QLibrary &lib, const char *func)
{
    if (auto result = (Signature)lib.resolve(func))
        return result;

    std::cout << "Error resolving function \"" << func << "\""
        << " from library " << lib.fileName().toStdString() << endl;

    throw resolve_error();
}

//
// Load a single plugin
//
RawDataPlugin load_plugin(const QString &name)
{
    QLibrary pluginLib(name);
    pluginLib.setLoadHints(QLibrary::ExportExternalSymbolsHint);

    if (!pluginLib.load())
    {
        cout << "Error loading plugin " << pluginLib.fileName().toStdString()
            << endl;
        throw library_load_error();
    }

    RawDataPlugin plugin = {};

    plugin.filename = pluginLib.fileName();
    plugin.info = resolve<PluginInfo>(pluginLib, "plugin_info");
    plugin.init = resolve<PluginInit>(pluginLib, "plugin_init");
    plugin.destroy = resolve<PluginDestroy>(pluginLib, "plugin_destroy");
    plugin.begin_run = resolve<BeginRun>(pluginLib, "begin_run");
    plugin.event_data = resolve<EventData>(pluginLib, "event_data");
    plugin.end_run = resolve<EndRun>(pluginLib, "end_run");

    {
        char *plugin_name = {};
        char *plugin_descr = {};

        plugin.info(&plugin_name, &plugin_descr);

        cout << "Loaded plugin from " << pluginLib.fileName().toStdString()
            << ": name=" << plugin_name
            << ", description=" << plugin_descr
            << endl;
    }

    plugin.userptr = plugin.init(plugin.filename.toStdString().c_str(), 0, nullptr);

    return plugin;
}

//
// process_listfile
//
static const size_t DataBufferCount = 2;
static const size_t DataBufferSize = Megabytes(1);

RunDescription * make_run_description(
    memory::Arena &arena,
    const QString &listfileFilename,
    const VMEConfig &vmeConfig)
{
    auto eventConfigs = vmeConfig.getEventConfigs();
    auto run = arena.push(RunDescription{});
    run->listfileFilename = arena.pushCString(listfileFilename.toStdString().c_str());
    run->events = arena.pushArray<EventReadoutDescription>(eventConfigs.size());
    run->eventCount = eventConfigs.size();

    for (int ei = 0; ei < eventConfigs.size(); ei++)
    {
        auto eventConfig = eventConfigs[ei];
        auto moduleConfigs = eventConfig->getModuleConfigs();
        auto &event = run->events[ei];
        event.name = arena.pushCString(eventConfig->objectName().toStdString().c_str());
        event.modules = arena.pushArray<ModuleReadoutDescription>(moduleConfigs.size());
        event.moduleCount = moduleConfigs.size();

        for (int mi = 0; mi < moduleConfigs.size(); mi++)
        {
            auto moduleConfig = moduleConfigs[ei];
            auto &module = event.modules[mi];

            module.name = arena.pushCString(
                moduleConfig->objectName().toStdString().c_str());

            module.type = arena.pushCString(
                moduleConfig->getModuleMeta().typeName.toStdString().c_str());

            auto moduleReadoutParts = mesytec::mvlc::parse_module_readout_script(
                mesytec::mvme::parse(moduleConfig->getReadoutScript()));

            module.prefixLen = moduleReadoutParts.prefixLen;
            module.suffixLen = moduleReadoutParts.suffixLen;
            module.hasDynamic = moduleReadoutParts.hasDynamic;
        }
    }

    return run;
}

class MVMEStreamCallbackWrapper: public IMVMEStreamModuleConsumer
{
    public:
        MVMEStreamCallbackWrapper(const mesytec::mvlc::ReadoutParserCallbacks &callbacks)
            : m_callbacks(callbacks)
        {
        }

        void beginRun(const RunInfo &runInfo,
                              const VMEConfig *vmeConfig,
                              const analysis::Analysis *analysis) override
        {
        }

        void endRun(const DAQStats &stats, const std::exception *e = nullptr) override
        {
        }

        void beginEvent(s32 eventIndex) override
        {
            m_callbacks.beginEvent(eventIndex);
        }

        void endEvent(s32 eventIndex) override
        {
            m_callbacks.endEvent(eventIndex);
        }

        void processModulePrefix(s32 eventIndex,
                                       s32 moduleIndex,
                                       const u32 *data, u32 size) override
        {
            m_callbacks.modulePrefix(eventIndex, moduleIndex, data, size);
        }

        void processModuleData(s32 eventIndex,
                                       s32 moduleIndex,
                                       const u32 *data, u32 size) override
        {
            m_callbacks.moduleDynamic(eventIndex, moduleIndex, data, size);
        }

        void processModuleSuffix(s32 eventIndex,
                                       s32 moduleIndex,
                                       const u32 *data, u32 size) override
        {
            m_callbacks.moduleSuffix(eventIndex, moduleIndex, data, size);
        }

        void processTimetick() override
        {
        }

        void setLogger(Logger logger) override
        {
        }


    private:
        mesytec::mvlc::ReadoutParserCallbacks m_callbacks;
};

void process_one_listfile(const QString &filename, const std::vector<RawDataPlugin> &plugins)
{
    auto replayHandle = open_listfile(filename);

    std::unique_ptr<VMEConfig> vmeConfig;
    std::error_code ec;

    std::tie(vmeConfig, ec) = read_vme_config_from_listfile(replayHandle);

    if (ec)
        throw ec;

    auto controllerType = vmeConfig->getControllerType();

    switch (replayHandle.format)
    {
        case ListfileBufferFormat::MVLC_ETH:
            if (controllerType != VMEControllerType::MVLC_ETH)
                throw std::runtime_error("listfile format and VME controller type mismatch 1");
            break;

        case ListfileBufferFormat::MVLC_USB:
            if (controllerType != VMEControllerType::MVLC_USB)
                throw std::runtime_error("listfile format and VME controller type mismatch 2");
            break;

        case ListfileBufferFormat::MVMELST:
            if (controllerType != VMEControllerType::SIS3153
                && controllerType != VMEControllerType::VMUSB)
            {
                throw std::runtime_error("listfile format and VME controller type mismatch 3");
            }
            break;
    };

    VMEControllerFactory ctrlFactory(controllerType);

    ThreadSafeDataBufferQueue emptyBuffers;
    ThreadSafeDataBufferQueue filledBuffers;
    // To make sure buffers are deleted when leaving this function
    std::vector<std::unique_ptr<DataBuffer>> buffers;

    for (size_t i=0; i<DataBufferCount; ++i)
    {
        buffers.emplace_back(std::make_unique<DataBuffer>(DataBufferSize));
        emptyBuffers.queue.push_back(buffers.back().get());
    }

    auto replayWorker = std::unique_ptr<ListfileReplayWorker>(ctrlFactory.makeReplayWorker(
            &emptyBuffers, &filledBuffers));

    if (!replayWorker)
        throw std::runtime_error("could not create replay worker");

    QThread replayThread;
    replayThread.setObjectName("mvme replay");
    replayWorker->moveToThread(&replayThread);

    std::atomic<bool> replayDone(false);

    QObject::connect(replayWorker.get(), &ListfileReplayWorker::replayStopped,
                     [&replayDone] { replayDone = true; });

    QObject::connect(replayWorker.get(), &ListfileReplayWorker::replayStopped,
                     &replayThread, &QThread::quit);

    using namespace mesytec::mvlc;

    memory::Arena arena(4096);
    auto runDescription = make_run_description(arena, filename, *vmeConfig);

    if (runDescription->eventCount <= 0)
        throw std::runtime_error("no event configs found in vme config from listfile");

    int maxModuleCount = std::max_element(
        runDescription->events, runDescription->events + runDescription->eventCount,
        [] (const auto &ea, const auto &eb)
        {
            return ea.moduleCount < eb.moduleCount;
        })->moduleCount;

    auto moduleDataList = arena.pushArray<ModuleData>(maxModuleCount);

    ReadoutParserCallbacks callbacks{};

    callbacks.beginEvent = [&] (int ei)
    {
        std::fill(moduleDataList, moduleDataList+maxModuleCount, ModuleData{});
    };
    callbacks.modulePrefix = [&] (int ei, int mi, const u32 *data, u32 size)
    {
        moduleDataList[mi].prefix = { data, size };
    };
    callbacks.moduleDynamic = [&] (int ei, int mi, const u32 *data, u32 size)
    {
        moduleDataList[mi].dynamic = { data, size };
    };
    callbacks.moduleSuffix = [&] (int ei, int mi, const u32 *data, u32 size)
    {
        moduleDataList[mi].suffix = { data, size };
    };

    callbacks.endEvent = [&] (int ei)
    {
        int moduleCount = runDescription->events[ei].moduleCount;

        for (auto &plugin: plugins)
        {
            plugin.event_data(plugin.userptr, ei, moduleDataList, moduleCount);
        }
    };

    MVMEStreamCallbackWrapper cbWrap(callbacks);
    MVMEStreamProcessor mvmeStreamProc;
    analysis::Analysis emptyAnalysis;
    auto logger = [] (const QString &msg) { cout << msg.toStdString() << endl; };

    if (replayHandle.format == ListfileBufferFormat::MVMELST)
    {
        mvmeStreamProc.attachModuleConsumer(&cbWrap);

        RunInfo runInfo;
        runInfo.isReplay = true;
        runInfo.runId = filename;

        ListFile lf(replayHandle.listfile.get());
        lf.open();

        emptyAnalysis.beginRun(runInfo, vmeConfig.get(), logger);

        mvmeStreamProc.beginRun(runInfo, &emptyAnalysis, vmeConfig.get(),
                                lf.getFileVersion(),
                                logger);
    }

    auto mvlcParser = make_readout_parser(collect_readout_scripts(*vmeConfig));

    replayWorker->setListfile(replayHandle.listfile.get());

    QObject::connect(&replayThread, &QThread::started,
                     replayWorker.get(), &ListfileReplayWorker::start);

    replayThread.start();


    // TODO: try-catch around all calls into the plugins
    try
    {
        for (auto &plugin: plugins)
            plugin.begin_run(plugin.userptr, runDescription);

        size_t buffersProcessed = 0;

        // TODO: start the replay thread then loop until replayDone is true
        // in the loop get a buffer from the filledBuffers queue and feed it to the
        // mvlc readout parser. Use a timeout when waiting for a full buffer.
        // Finally put the buffer onto the emptyQueue.

        // Loop until the replayWorker signaled that it's done reading and there
        // are no more buffers left to process.
        while (!(replayDone && is_empty(&filledBuffers)))
        {
            // Try to get a buffer from the queue and hand it to processing.
            while (auto buffer = dequeue(&filledBuffers, 10))
            {
                ParseResult pr{};
                switch (controllerType)
                {
                    case VMEControllerType::MVLC_ETH:
                        pr = parse_readout_buffer_eth(
                            mvlcParser, callbacks,
                            buffer->id, buffer->data, buffer->used);
                        break;

                    case VMEControllerType::MVLC_USB:
                        pr = parse_readout_buffer_usb(
                            mvlcParser, callbacks,
                            buffer->id, buffer->data, buffer->used);
                        break;

                    case VMEControllerType::VMUSB:
                    case VMEControllerType::SIS3153:
                        mvmeStreamProc.processDataBuffer(buffer);
                        break;
                }

                // TODO: count parse results and display stats at the end
                (void) pr;

                enqueue(&emptyBuffers, buffer);
                ++buffersProcessed;
            }
        }

        // TODO: check that number of read buffers equals number of processed buffers

        for (auto &plugin: plugins)
            plugin.end_run(plugin.userptr);

        if (replayHandle.format == ListfileBufferFormat::MVMELST)
        {
            mvmeStreamProc.endRun(DAQStats{});
        }

        cout << "replayDone=" << replayDone << ", buffersProcessed=" <<
            buffersProcessed << endl;
    }
    catch (const std::exception &e)
    {
        replayWorker->stop();
        replayThread.quit();
        replayThread.wait();
        throw;
    }

    replayThread.quit();
    replayThread.wait();

    // One side could be a ListfileReplayWorker which is moved to its own
    // thread. This is the buffer producer side

    // In the middle is a buffer queue of 2 or more buffers.

    // The consumer could be this main thread.
    //
    // For MVLC consumer code could look like this:
    // Setup ReadoutParserCallbacks for the readout parser and optionally setup
    // a multi event splitter. Get the splitter filter strings from the
    // VATS.
    // Create a readout parser using the VMEConfig.
    // Call the parser for each buffer in the filled queue:
    //      pr = parse_readout_buffer_eth(
    //          m_parser, m_parserCallbacks,
    //          buffer->id, buffer->data, buffer->used);
    // or
    //      pr = parse_readout_buffer_usb(
    //          m_parser, m_parserCallbacks,
    //          buffer->id, buffer->data, buffer->used);
    //
    // MVMELST consumer:
    // This could be done by implementing a IMVMEStreamModuleConsumer and
    // attaching this to an instance of MVMEStreamWorker.
    // This would also work for the MVLC side as the MVLC_StreamWorker supports.
}

int main(int argc, char *argv[])
{
    QCoreApplication theQtApp(argc, argv);

    std::vector<QString> inputFilenames;
    std::vector<QString> pluginSpecs;
    std::vector<RawDataPlugin> plugins;

    for (int i = 1; i < argc; i++)
    {
        inputFilenames.emplace_back(QString(argv[i]));
    }

    try
    {
        auto plugin = load_plugin("listfile_reader_print_plugin");
        plugins.emplace_back(plugin);
    }
    catch (const resolve_error &)
    {
        //return 1;
    }

#if 1
    try
    {
        auto plugin = load_plugin("listfile_reader_python_plugin");
        plugins.emplace_back(plugin);
    }
    catch (const resolve_error &)
    {
        //return 1;
    }
#endif

    if (plugins.empty())
    {
        cout << "Error: no plugins could be loaded" << endl;
        return 1;
    }

    // For each listfile:
    //   open the file for reading (zip/non-zip should work)
    //   read the vme config from the file, get the vme controller type, create the factory
    for (const auto &listfileFilename: inputFilenames)
    {
        try
        {
            //if (auto ec = process_one_listfile(listfileFilename))
            //    throw ec;
            process_one_listfile(listfileFilename, plugins);
        }
        catch (const std::error_code &ec)
        {
            cout << "Error processing file: " << listfileFilename.toStdString()
                << ": " << ec.message() << endl;
        }
        catch (const std::exception &e)
        {
            cout << "Error processing file: " << listfileFilename.toStdString()
                << ": " << e.what() << endl;
        }
        catch (const QString &msg)
        {
            cout << "Error processing file: " << listfileFilename.toStdString()
                << ": " << msg.toStdString() << endl;
        }
    }

    for (auto &plugin: plugins)
    {
        plugin.destroy(plugin.userptr);
    }

    return 0;
}
