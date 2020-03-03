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
#include "mvme.h"
#include "mvme_session.h"
#include "vme_controller_factory.h"
#include "analysis/analysis_util.h"

#include <getopt.h>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>

static QTextStream &print_options(QTextStream &out, struct option *opts)
{
    out << "Available command line options: ";
    bool needComma = false;

    for (; opts->name; ++opts)
    {
        if (strcmp(opts->name, "help") == 0)
            continue;

        if (needComma)
            out << ", ";

        out << opts->name;
        needComma = true;
    }
    out << endl;

    return out;
}

struct ModuleTemplateTestInfo
{
    QString workspacePath;
    VMEControllerType controllerType = VMEControllerType::VMUSB;
    QVariantMap controllerOptions;
    QString moduleType;
    u32 moduleAddress = 0x0;
};

static const QMap<QString, QString> AdditionalModuleSettings =
{
    { "madc32",         "0x6070 0b111\n"
    },
    { "mqdc32",         "0x6070 0b101\n"
                        "0x6072 32\n"
    },
    { "mtdc32",         "0x6070 3\n"
    },
    { "mdpp16",         "0x6070 1\n"
    },
    { "mdpp16_rcp",     "0x6070 1\n"
                        "0x6072 400\n"
    },
    { "mdpp16_qdc",     "0x6070 1\n"
                        "0x6072 400\n"
    },
};

static struct option long_options[] = {
    { "controller-type",        required_argument,      nullptr,    0 },
    { "controller-options",     required_argument,      nullptr,    0 },
    { "module-type",            required_argument,      nullptr,    0 },
    { "module-address",         required_argument,      nullptr,    0 },
    { "workspace-path",         required_argument,      nullptr,    0 },
    { "help",                   no_argument,            nullptr,    0 },
    { nullptr, 0, nullptr, 0 },
};

static QTextStream out(stdout);
static QTextStream err(stderr);

int main(int argc, char *argv[])
{
    using namespace analysis;

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/window_icon.png"));

    //
    // fill testInfo from command line args
    //
    ModuleTemplateTestInfo testInfo;

    while (true)
    {
        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c != 0)
            break;

        QString opt_name(long_options[option_index].name);

        if (opt_name == "help")
        {
            print_options(out, long_options);
            return 0;
        }
        else if (opt_name == "controller-type")
        {
            testInfo.controllerType = from_string(optarg);
        }
        else if (opt_name == "controller-options")
        {
            auto parts = QString(optarg).split(",");
            for (auto part: parts)
            {
                auto key_value = part.split('=');
                if (key_value.size() == 2)
                {
                    testInfo.controllerOptions[key_value[0]] = key_value[1];
                }
            }
        }
        else if (opt_name == "module-type")
        {
            testInfo.moduleType = optarg;
        }
        else if (opt_name == "module-address")
        {
            testInfo.moduleAddress = QString(optarg).toUInt(nullptr, 0);
        }
        else if (opt_name == "workspace-path")
        {
            testInfo.workspacePath = optarg;
        }
    }

    //
    // check parsed args
    //
    if (testInfo.moduleType.isEmpty())
    {
        err << "Missing module-type!" << endl;
        return 1;
    }

    // create daq config
    // create module, add to config
    // instantiate templates
    // set irq, set pulser
    // create controller, set on context
    // set vme config on context
    // start everything via the singleshot timer
    int ret = 0;

    try
    {
        // init mvme using a non-default app name to not mess with the standard mvme.conf
        mvme_init("mvme_test_launcher_module_template");
        MVMEMainWindow w;
        auto context = w.getContext();

        if (!testInfo.workspacePath.isEmpty())
        {
            context->openWorkspace(testInfo.workspacePath);
        }

        // VME controller
        context->setVMEController(testInfo.controllerType, testInfo.controllerOptions);
        auto ctrl = context->getVMEController();
        auto error = ctrl->open();
        if (error.isError())
            throw error;

        //
        // VME config
        //
        auto mvmeTemplates = vats::read_templates();

        // module
        auto moduleMeta = get_module_meta_by_typename(mvmeTemplates, testInfo.moduleType);

        if (moduleMeta.typeId == vats::VMEModuleMeta::InvalidTypeId)
            throw QString("No templates found for module type %1").arg(testInfo.moduleType);

        auto moduleConfig = new ModuleConfig;
        moduleConfig->setObjectName(testInfo.moduleType);
        moduleConfig->setModuleMeta(moduleMeta);
        moduleConfig->setBaseAddress(testInfo.moduleAddress);
        moduleConfig->getReadoutScript()->setObjectName(moduleMeta.templates.readout.name);
        moduleConfig->getReadoutScript()->setScriptContents(moduleMeta.templates.readout.contents);
        moduleConfig->getResetScript()->setObjectName(moduleMeta.templates.reset.name);
        moduleConfig->getResetScript()->setScriptContents(moduleMeta.templates.reset.contents);
        for (const auto &vmeTemplate: moduleMeta.templates.init)
        {
            moduleConfig->addInitScript(new VMEScriptConfig(vmeTemplate.name, vmeTemplate.contents));
        }

        /* Instead of modifying the loaded scripts directly an additional init
         * script is appended which contains the modified settings required for
         * the test run. */
        QString moduleSetupScript = QString("0x6010   1 # IRQ\n");
        moduleSetupScript += AdditionalModuleSettings.value(testInfo.moduleType, QString());
        moduleConfig->addInitScript(new VMEScriptConfig("Module Test Setup", moduleSetupScript));

        // event
        auto eventTemplates = mvmeTemplates.eventTemplates;
        auto eventConfig = new EventConfig;
        eventConfig->setObjectName("TestEvent");
        eventConfig->triggerCondition = TriggerCondition::Interrupt;
        eventConfig->irqLevel = 1;
        eventConfig->addModuleConfig(moduleConfig);
        eventConfig->vmeScripts["daq_start"]->setScriptContents(eventTemplates.daqStart.contents);
        eventConfig->vmeScripts["daq_stop"]->setScriptContents(eventTemplates.daqStop.contents);
        eventConfig->vmeScripts["daq_stop"]->addToScript(
            "writeabs a32 d16 0xbb006070 0\n"); // turn off the pulser using the broadcast address
        eventConfig->vmeScripts["readout_start"]->setScriptContents(eventTemplates.readoutCycleStart.contents);
        eventConfig->vmeScripts["readout_end"]->setScriptContents(eventTemplates.readoutCycleEnd.contents);

        // vme config
        auto vmeConfig = new VMEConfig;
        vmeConfig->addEventConfig(eventConfig);
        vmeConfig->setModified(false);
        context->setVMEConfig(vmeConfig);

        // analysis
        auto extractors = analysis::get_default_data_extractors(testInfo.moduleType);

        for (auto &ex: extractors)
        {
            auto dataFilter = ex->getFilter();
            double unitMin = 0.0;
            double unitMax = std::pow(2.0, dataFilter.getDataBits());
            QString name = moduleConfig->getModuleMeta().typeName + QSL(".") + ex->objectName().section('.', 0, -1);

            RawDataDisplay rawDataDisplay = make_raw_data_display(dataFilter, unitMin, unitMax,
                                                                  name,
                                                                  ex->objectName().section('.', 0, -1),
                                                                  QString());

            add_raw_data_display(context->getAnalysis(), eventConfig->getId(),
                                 moduleConfig->getId(), rawDataDisplay);
        }

        context->getAnalysis()->setModified(false);

        // show the main window
        w.show();
        w.restoreSettings();

        QTimer::singleShot(0, [&w]() {
            // code to be run on entering the event loop for the first time
        });

        ret = app.exec();
        mvme_shutdown();
    }
    catch (const VMEError &e)
    {
        err << "Error: " << e.toString() << endl;
        ret = 1;
    }
    catch (const QString &e)
    {
        err << "Error: " << e << endl;
        ret = 1;
    }

    return ret;
}
