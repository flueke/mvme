/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
- Load user specified code and invoke it with data extracted from the listfile.

TODO:
- somehow let the user enable multi event splitting and load appropriate
  splitting filters for each module
- Better command line parsing
- Improve error handling.

Notes:
- There is no need to specify the library extension when using
  QPluginLoader or QLibrary.

- C-style libraries need to be wrapped in an extern "C" block if compiled using
  a c++ compiler.

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

using std::cerr;
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

struct library_load_error: public std::runtime_error
{
    using runtime_error::runtime_error;
};

struct resolve_error: public std::runtime_error
{
    using runtime_error::runtime_error;
};

template<typename Signature>
Signature resolve(QLibrary &lib, const char *func)
{
    if (auto result = (Signature)lib.resolve(func))
        return result;

    std::cout << "Error resolving function \"" << func << "\""
        << " from library " << lib.fileName().toStdString() << endl;

    throw resolve_error(std::string("resolve_error ") + func);
}

//
// Load a single plugin
//
RawDataPlugin load_plugin(const QString &name, const std::vector<std::string> &pluginArgs)
{
    QLibrary pluginLib(name);
    pluginLib.setLoadHints(QLibrary::ExportExternalSymbolsHint);

    if (!pluginLib.load())
    {
        cout << "Error loading plugin " << pluginLib.fileName().toStdString()
            << ": " << pluginLib.errorString().toStdString()
            << endl;
        throw library_load_error("library_load_error " + pluginLib.errorString().toStdString());
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

        cout << "Loading plugin from " << pluginLib.fileName().toStdString()
            << ": name=" << plugin_name
            << ", description=" << plugin_descr
            << endl;
    }

    auto args = std::make_unique<const char *[]>(pluginArgs.size());

    for (size_t i=0; i<pluginArgs.size(); i++)
        args[i] = pluginArgs[i].data();

    plugin.userptr = plugin.init(
        plugin.filename.toStdString().c_str(),
        pluginArgs.size(), args.get());

    return plugin;
}

//
// process_listfile
//
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
            auto moduleConfig = moduleConfigs[mi];
            auto &module = event.modules[mi];

            module.name = arena.pushCString(
                moduleConfig->objectName().toStdString().c_str());

            module.type = arena.pushCString(
                moduleConfig->getModuleMeta().typeName.toStdString().c_str());

            auto moduleReadoutParts = mesytec::mvme_mvlc::parse_module_readout_script(
                mesytec::mvme::parse(moduleConfig->getReadoutScript()));

            module.len = moduleReadoutParts.len;
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

        void beginEvent(s32 /*eventIndex*/) override
        {
            std::fill(m_moduleDataList.begin(), m_moduleDataList.end(), ModuleData{});
        }

        void processModuleData(
            s32 /*eventIndex*/, s32 moduleIndex, const u32 *data, u32 size) override
        {
            if (moduleIndex < static_cast<s32>(m_moduleDataList.size()))
                m_moduleDataList[moduleIndex].data = { data, size };
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
        void setLogger(Logger /*logger*/) override { }

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
    /*auto& replayHandle =*/ context_open_listfile(&mvmeContext, filename, {});

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
                     [&loop] (AnalysisWorkerState state)
                     {
                         if (state == AnalysisWorkerState::Idle)
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
    QApplication app(argc, argv);

    if (argc <= 1)
    {
        cerr << "Usage: listfile_reader <listfile> [<plugin> [<plugin_arg0>, <plugin_arg1>, ...]]" << endl;
        return 1;
    }

    if (argv[1] == std::string("--help") || argv[1] == std::string("-h"))
    {
        cerr << "Usage: listfile_reader <listfile> [<plugin> [<plugin_arg0>, <plugin_arg1>, ...]]" << endl;
        return 0;
    }

    QString inputFilename(argv[1]);
    QString pluginName("listfile_reader_print_plugin");
    std::vector<std::string> pluginArgs;

    if (argc > 2)
    {
        pluginName = argv[2];

        for (int i=3; i<argc; ++i)
        {
            pluginArgs.push_back(argv[i]);
        }
    }

    RawDataPlugin pluginInstance;

    try
    {
        pluginInstance = load_plugin(pluginName, pluginArgs);
    }
    catch (const std::runtime_error &e)
    {
        cerr << "Error loading plugin " << pluginName.toStdString()
            << ": " << e.what() << endl;
        return 1;
    }

    assert(!pluginInstance.filename.isEmpty());

    mvme_init(argv[0]);

    MVMEContext mvmeContext;

    try
    {
        process_one_listfile(inputFilename, { pluginInstance }, mvmeContext);
    }
    catch (const std::error_code &ec)
    {
        cout << "Error processing file: " << inputFilename.toStdString()
            << ": " << ec.message() << endl;
    }
    catch (const std::exception &e)
    {
        cout << "Error processing file: " << inputFilename.toStdString()
            << ": " << e.what() << endl;
    }
    catch (const QString &msg)
    {
        cout << "Error processing file: " << inputFilename.toStdString()
            << ": " << msg.toStdString() << endl;
    }

    pluginInstance.destroy(pluginInstance.userptr);

    return 0;
}
