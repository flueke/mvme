/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include <QApplication>
#include <getopt.h>

#include "mvme_context.h"
#include "vme_controller_factory.h"
#ifdef MVME_USE_GIT_VERSION_FILE
#include "git_sha1.h"
#endif

using RegisterMap = QMap<u32, QString>;

RegisterMap read_register_map(const QString &filename)
{
    RegisterMap result;

    QFile inFile(filename);

    if (!inFile.open(QIODevice::ReadOnly))
        throw inFile.errorString();

    QTextStream inStream(&inFile);

    while (true)
    {
        u32 address;
        QString name;
        inStream >> address >> name;
        if (inStream.status() == QTextStream::Ok)
        {
            result[address] = name;
        }
        else
        {
            break;
        }
    }

    return result;
}

/* mvme_register_reader - Reads a list of registers from a vme device. Outputs the formatted results.
 *
 * Usage example:
 * ./mvme_register_reader --controller-type=SIS3153 --controller-options=hostname=sis3153-0040 \
 *                        --module-type=mdpp-16_qdc --module-address=0x01000000 \
 *                        --module-register-map=mdpp-16_qdc.registermap
 */


int main(int argc, char *argv[])
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<GlobalMode>("GlobalMode");
    qRegisterMetaType<MVMEStreamWorkerState>("MVMEStreamWorkerState");
    qRegisterMetaType<ControllerState>("ControllerState");

    QApplication app(argc, argv);

    VMEControllerType controllerType = VMEControllerType::VMUSB;
    QVariantMap controllerOptions;
    QString moduleType;
    u32 moduleAddress = 0x0;
    RegisterMap moduleRegisterMap;

    QTextStream out(stdout);
    QTextStream err(stderr);

    while (true)
    {
        static struct option long_options[] = {
            { "controller-type",        required_argument,      nullptr,    0 },
            { "controller-options",     required_argument,      nullptr,    0 },
            { "module-type",            required_argument,      nullptr,    0 },
            { "module-address",         required_argument,      nullptr,    0 },
            { "module-register-map",    required_argument,      nullptr,    0 },

            // end of options
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c != 0)
            break;

        QString opt_name(long_options[option_index].name);

        qDebug() << opt_name << optarg;

        if (opt_name == "controller-type")
        {
            controllerType = from_string(optarg);
        }

        if (opt_name == "controller-options")
        {
            auto parts = QString(optarg).split(",");
            for (auto part: parts)
            {
                auto key_value = part.split('=');
                if (key_value.size() == 2)
                {
                    controllerOptions[key_value[0]] = key_value[1];
                }
            }
        }

        if (opt_name == "module-type")
        {
            moduleType = opt_name;
        }

        if (opt_name == "module-address")
        {
            moduleAddress = QString(optarg).toUInt(nullptr, 0);
        }

        if (opt_name == "module-register-map")
        {
            try
            {
                moduleRegisterMap = read_register_map(optarg);
            }
            catch (const QString &e)
            {
                err << "Error reading module register map from " << optarg
                    << ": " << e << endl;
                return 1;
            }
        }
    }

    /*
     * create controller
     * connect
     * for each entry in the register map:
     *   read register
     *   output register address, name and value
     */

    try
    {

        VMEControllerFactory controllerFactory(controllerType);
        std::unique_ptr<VMEController> controller(controllerFactory.makeController(controllerOptions));

        auto error = controller->open();
        if (error.isError())
            throw error;

        for (auto regAddr: moduleRegisterMap.keys())
        {
            u32 fullAddress = moduleAddress + regAddr;
            u16 regValue = 0;

            error = controller->read16(fullAddress, &regValue, vme_address_modes::a32UserData);
            if (error.isError())
                throw error;

            // addr \t hex value \t dec value \t name \n

            out << (QString("0x%1\t0x%2\t%3\t%4")
                    .arg(regAddr,  4, 16, QLatin1Char('0'))
                    .arg(regValue, 4, 16, QLatin1Char('0'))
                    .arg(regValue, 6, 10)
                    .arg(moduleRegisterMap[regAddr])
                   )
                << endl;
        }
    }
    catch (const VMEError &e)
    {
        err << e.toString() << endl;
        return 1;
    }

    return 0;
}
