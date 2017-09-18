/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "mvme_startup.h"
#include "vme_controller_factory.h"

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
    VMEControllerType controllerType = VMEControllerType::VMUSB;
    QVariantMap controllerOptions;
    QString moduleType;
    u32 moduleAddress = 0x0;
};

static struct option long_options[] = {
    { "controller-type",        required_argument,      nullptr,    0 },
    { "controller-options",     required_argument,      nullptr,    0 },
    { "module-type",            required_argument,      nullptr,    0 },
    { "module-address",         required_argument,      nullptr,    0 },
    { "help",                   no_argument,            nullptr,    0 },
    { nullptr, 0, nullptr, 0 },
};

static QTextStream out(stdout);
static QTextStream err(stderr);

int main(int argc, char *argv[])
{
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

        if (opt_name == "controller-type")
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
            testInfo.moduleType = opt_name;
        }
        else if (opt_name == "module-address")
        {
            testInfo.moduleAddress = QString(optarg).toUInt(nullptr, 0);
        }
        else if (opt_name == "help")
        {
            print_options(out, long_options);
            return 0;
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
        mvme_basic_init("mvme_test_launcher_module_template");
        mvme w;
        auto context = w.getContext();

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
        auto moduleMeta    = get_module_meta_by_typename(mvmeTemplates, testInfo.moduleType);

        if (moduleMeta.typeId == vats::VMEModuleMeta::InvalidTypeId)
            throw QString("No templates found for module type %1").arg(testInfo.moduleType);

        auto moduleConfig = new ModuleConfig;
        moduleConfig->setModuleMeta(moduleMeta);
        moduleConfig->setBaseAddress(testInfo.moduleAddress);
        module->getReadoutScript()->setObjectName(moduleMeta.templates.readout.name);
        module->getReadoutScript()->setScriptContents(moduleMeta.templates.readout.contents);
        module->getResetScript()->setObjectName(moduleMeta.templates.reset.name);
        module->getResetScript()->setScriptContents(moduleMeta.templates.reset.contents);

        // event
        auto eventConfig = new EventConfig;

        // vme config
        auto vmeConfig = new VMEConfig;

        // show the main window
        w.show();
        w.restoreSettings();

        QTimer::singleShot(0, [&w]() {
            // code to be run on entering the event loop for the first time
        });

        ret = app.exec();
    }
    catch (const VMEError &e)
    {
        err << e.toString() << endl;
        ret = 1;
    }
    catch (const QString &e)
    {
        err << e << endl;
        ret = 1;
    }

    return ret;
}
