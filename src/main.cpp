#include "mvme.h"
#include "util.h"
#include "vmusb_stack.h"
#include "mvme_context.h"
#include "vmecontroller.h"
#include "daqconfig_tree.h"
#include "mvme_config.h"

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>

#include <QJsonObject>
#include <QJsonDocument>

int main(int argc, char *argv[])
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<DAQState>("GlobalMode");
    qRegisterMetaType<DAQState>("ControllerState");
    QApplication a(argc, argv);

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName("mvme");
    QCoreApplication::setApplicationVersion("0.2.0");

    qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
    qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);

    mvme w;
    w.show();
    w.restoreSettings();

    //
    // test
    //
#if 0
    DAQConfig *config = new DAQConfig;

    {
        auto script = new VMEScriptConfig(config);
        script->setObjectName("start 1");
        script->setScriptContents("the contents");
        config->vmeScriptLists["daq_start"].push_back(script);
    }

    {
        auto script = new VMEScriptConfig(config);
        script->setObjectName("start 2");
        script->setScriptContents("the contents");
        config->vmeScriptLists["daq_start"].push_back(script);
    }

    {
        auto script = new VMEScriptConfig(config);
        script->setObjectName("end 1");
        script->setScriptContents("the contents");
        config->vmeScriptLists["daq_end"].push_back(script);
    }

    {
        auto script = new VMEScriptConfig(config);
        script->setObjectName("end 2");
        script->setScriptContents("the contents");
        config->vmeScriptLists["daq_end"].push_back(script);
    }

    {
        auto script = new VMEScriptConfig(config);
        script->setObjectName("manual 1");
        script->setScriptContents("the contents");
        config->vmeScriptLists["manual"].push_back(script);
    }

    {
        auto script = new VMEScriptConfig(config);
        script->setObjectName("manual 2");
        script->setScriptContents("the contents");
        config->vmeScriptLists["manual"].push_back(script);
    }

    {
        auto event = new EventConfig(config);
        event->setObjectName("event0");

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("daq start");
            script->setScriptContents("the contents");
            event->vmeScripts["daq_start"] = script;
        }

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("daq end");
            script->setScriptContents("the contents");
            event->vmeScripts["daq_end"] = script;
        }

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("readout start");
            script->setScriptContents("the contents");
            event->vmeScripts["readout_start"] = script;
        }

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("readout end");
            script->setScriptContents("the contents");
            event->vmeScripts["readout_end"] = script;
        }

        {
            auto module = new ModuleConfig(event);
            module->setObjectName("mdpp16");
            module->type = VMEModuleType::MDPP16;
            module->baseAddress = 0x43210000;
            event->modules.push_back(module);
            module->vmeScripts["parameters"] = new VMEScriptConfig(module);
        }

        {
            auto module = new ModuleConfig(event);
            module->setObjectName("my qdc");
            module->type = VMEModuleType::MQDC32;
            module->baseAddress = 0xbeef0000;
            event->modules.push_back(module);
        }

        config->eventConfigs.push_back(event);
    }

    {
        auto event = new EventConfig(config);
        event->setObjectName("event1");

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("daq start");
            script->setScriptContents("the contents");
            event->vmeScripts["daq_start"] = script;
        }

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("daq end");
            script->setScriptContents("the contents");
            event->vmeScripts["daq_end"] = script;
        }

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("readout start");
            script->setScriptContents("the contents");
            event->vmeScripts["readout_start"] = script;
        }

        {
            auto script = new VMEScriptConfig(event);
            script->setObjectName("readout end");
            script->setScriptContents("the contents");
            event->vmeScripts["readout_end"] = script;
        }

        {
            auto module = new ModuleConfig(event);
            module->setObjectName("mdpp16_1");
            module->type = VMEModuleType::MDPP16;
            module->baseAddress = 0x43210000;
            event->modules.push_back(module);
            module->vmeScripts["parameters"] = new VMEScriptConfig(module);
        }

        {
            auto module = new ModuleConfig(event);
            module->setObjectName("the other qdc");
            module->type = VMEModuleType::MQDC32;
            module->baseAddress = 0xbeef0000;
            event->modules.push_back(module);
        }

        config->eventConfigs.push_back(event);
    }

    DAQConfigTreeWidget configTreeWidget(w.getContext());
    configTreeWidget.setConfig(config);
    configTreeWidget.resize(800, 600);
    configTreeWidget.show();

    int ret = a.exec();

    QJsonObject json;
    config->write(json);
    QJsonDocument doc(json);

    auto dbg(qDebug());
    dbg.noquote();
    dbg << doc.toJson();

    delete config;
#endif

    int ret = a.exec();

    return ret;
}
