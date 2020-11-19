/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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

- How to handle system events? These do not really fit into the event.module
  scheme.

*/

#include <iostream>
#include <vector>

#include <QApplication>
#include <QLibrary>


#include "analysis/analysis.h"
#include "daqcontrol.h"
#include "listfile_reader/listfile_reader.h"
#include "mvlc/readout_parser_support.h"
#include "mvme_context.h"
#include "mvme_context_lib.h"
#include "mvme_session.h"
#include "util_zip.h"
#include "vme_config_scripts.h"

using std::cout;
using std::endl;
using namespace mesytec;

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

            auto moduleReadoutParts = mesytec::mvme_mvlc::parse_module_readout_script(
                mesytec::mvme::parse(moduleConfig->getReadoutScript()));

            module.prefixLen = moduleReadoutParts.prefixLen;
            module.suffixLen = moduleReadoutParts.suffixLen;
            module.hasDynamic = moduleReadoutParts.hasDynamic;
        }
    }

    return run;
}

class ModuleDataConsumer: public IMVMEStreamModuleConsumer
{
    public:
        ModuleDataConsumer(
            RunDescription *runDescription,
            const std::vector<RawDataPlugin> &plugins)
          : m_runDescription(runDescription)
          , m_plugins(plugins)
        {
            int maxModuleCount = std::max_element(
                runDescription->events, runDescription->events + runDescription->eventCount,
                [] (const auto &ea, const auto &eb)
                {
                    return ea.moduleCount < eb.moduleCount;
                })->moduleCount;

            m_moduleDataList.resize(maxModuleCount);
        }

        void beginRun(const RunInfo &, const VMEConfig *, const analysis::Analysis *) override {}
        void endRun(const DAQStats &, const std::exception * = nullptr) override {}

        void beginEvent(s32 eventIndex) override
        {
            std::fill(m_moduleDataList.begin(), m_moduleDataList.end(), ModuleData{});
        }

        void processModulePrefix(
            s32 /*eventIndex*/, s32 moduleIndex, const u32 *data, u32 size) override
        {
            if (moduleIndex < static_cast<s32>(m_moduleDataList.size()))
                m_moduleDataList[moduleIndex].prefix = { data, size };
        }

        void processModuleData(
            s32 /*eventIndex*/, s32 moduleIndex, const u32 *data, u32 size) override
        {
            if (moduleIndex < static_cast<s32>(m_moduleDataList.size()))
                m_moduleDataList[moduleIndex].dynamic = { data, size };
        }

        void processModuleSuffix(
            s32 /*eventIndex*/, s32 moduleIndex, const u32 *data, u32 size) override
        {
            if (moduleIndex < static_cast<s32>(m_moduleDataList.size()))
                m_moduleDataList[moduleIndex].suffix = { data, size };
        }

        void endEvent(s32 ei) override
        {
            int moduleCount = m_runDescription->events[ei].moduleCount;

            for (auto &plugin: m_plugins)
            {
                plugin.event_data(plugin.userptr, ei, m_moduleDataList.data(), moduleCount);
            }
        }

        void processTimetick() override { }
        void setLogger(Logger logger) override { }

    private:
        RunDescription *m_runDescription;
        std::vector<ModuleData> m_moduleDataList;
        const std::vector<RawDataPlugin> &m_plugins;
};

void process_one_listfile(
    const QString &filename,
    const std::vector<RawDataPlugin> &plugins,
    MVMEContext &mvmeContext)
{
    // FIXME: how does error reporting work here?
    /*auto& replayHandle =*/ context_open_listfile(&mvmeContext, filename, 0);

    memory::Arena arena(4096);
    auto runDescription = make_run_description(arena, filename, *mvmeContext.getVMEConfig());

    if (runDescription->eventCount <= 0)
        throw std::runtime_error("no event configs found in vme config from listfile");

    ModuleDataConsumer dataConsumer(runDescription, plugins);

    mvmeContext.getMVMEStreamWorker()->attachModuleConsumer(&dataConsumer);

    // TODO: try-catch around all calls into the plugins

    for (auto &plugin: plugins)
        plugin.begin_run(plugin.userptr, runDescription);


    QEventLoop loop;

    QObject::connect(&mvmeContext, &MVMEContext::mvmeStreamWorkerStateChanged,
                     [&loop] (MVMEStreamWorkerState state)
                     {
                         if (state == MVMEStreamWorkerState::Idle)
                             loop.quit();
                     });

    DAQControl daqControl(&mvmeContext);
    daqControl.startDAQ();
    loop.exec(); // block here until the replay is done

    for (auto &plugin: plugins)
        plugin.end_run(plugin.userptr);
}

int main(int argc, char *argv[])
{
    // TODO: use lyra to parse the command line

    QApplication app(argc, argv);

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

#if 0
    try
    {
        auto plugin = load_plugin("listfile_reader_python_plugin");
        plugins.emplace_back(plugin);
    }
    // FIXME: this didn't catch some library_load error.
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

    mvme_init(argv[0]);

    MVMEContext mvmeContext;

    // For each listfile:
    //   open the file for reading (zip/non-zip should work)
    //   read the vme config from the file, get the vme controller type, create the factory
    for (const auto &listfileFilename: inputFilenames)
    {
        try
        {
            //if (auto ec = process_one_listfile(listfileFilename))
            //    throw ec;
            process_one_listfile(listfileFilename, plugins, mvmeContext);
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
