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
#include <iostream>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>

#include "mesytec-mvlc/mvlc_factory.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_vme_controller.h"
#include "vme_script.h"
#include "vme_script_exec.h"

using namespace mesytec;
using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCommandLineOption usbOption("usb", "USB device index to use.", "usb_device_index", "0");
    QCommandLineOption ethOption("eth", "Hostname/IP address to connect to.", "eth_hostname");

    QCommandLineOption repetitionOption(
        "repetitions",
        "Number of times to run the given script. Use 0 to run forever.",
        "repetitions",
        "1");

    QCommandLineOption verboseOption(
        {"v", "verbose"},
        "Enable verbose output.");

    QCommandLineParser parser;
    parser.addPositionalArgument("scriptfile", "VME Script file to run");
    parser.addOptions({ usbOption, ethOption, repetitionOption, verboseOption });
    parser.addHelpOption();

    parser.process(app);

    if (parser.positionalArguments().isEmpty())
    {
        cout << "Error: missing script file to execute." << endl;
        return 1;
    }

    vme_script::VMEScript vmeScript;

    {
        QString scriptFilename = parser.positionalArguments().at(0);
        QFile scriptFile(scriptFilename);
        if (!scriptFile.open(QIODevice::ReadOnly))
        {
            cout << "Error opening vme script file " << scriptFilename.toStdString()
                << " for reading." << endl;
            return 1;
        }

        vmeScript = vme_script::parse(&scriptFile);
    }

    mvlc::MVLC mvlc;

    if (parser.isSet(ethOption))
    {
        auto hostname = parser.value(ethOption);
        mvlc = mvlc::make_mvlc_eth(hostname.toStdString());
    }
    else // default to usb
    {
        unsigned index = parser.value(usbOption).toUInt();
        mvlc = mvlc::make_mvlc_usb(index);
    }

    const unsigned repetitions = parser.value(repetitionOption).toUInt();
    const bool verbose = parser.isSet(verboseOption);

    mvme_mvlc::MVLCObject mvlcObj(mvlc);
    mvme_mvlc::MVLC_VMEController mvlcCtrl(&mvlcObj);

    if (auto err = mvlcCtrl.open())
    {
        cout << "Error opening mvlc: " << err.toString().toStdString() << endl;
        return 1;
    }

    auto logger = [] (const QString &msg)
    {
        cout << msg.toStdString() << endl;
    };

    //mvlcCtrl.disableNotificationPolling();

    const int commandCount = vmeScript.size();
    std::chrono::milliseconds minElapsed = std::chrono::milliseconds::max();
    std::chrono::milliseconds maxElapsed = {};

    for (unsigned rep = 0; rep < repetitions || repetitions == 0; ++rep)
    {
        cout << "Script run #" << rep + 1 << "/";
        if (repetitions == 0)
            cout << "inf";
        else
            cout << repetitions;
        cout << "... ";
        cout.flush();

        u8 opts = vme_script::run_script_options::AbortOnError;

        if (verbose)
            opts |= vme_script::run_script_options::LogEachResult;

        auto tStart = std::chrono::high_resolution_clock::now();
        auto results = vme_script::run_script(&mvlcCtrl, vmeScript, logger, opts);
        auto tEnd = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart);

        cout << "t=" << elapsed.count() << "ms"
            //<< ", #commands=" << commandCount
            << ", " << elapsed.count() / static_cast<float>(commandCount) << " ms/cmd"
            << endl;

        minElapsed = std::min(minElapsed, elapsed);
        maxElapsed = std::max(maxElapsed, elapsed);

        if (has_errors(results))
        {
            auto it = std::find_if(results.begin(), results.end(),
                                   [] (const auto &r) { return r.error.isError(); });
            if (it != results.end())
            {
                cout << "Error: " << it->error.toString().toStdString() << endl;
            }
            return 1;
        }
    }

    cout << "minElapsed=" << minElapsed.count()
        << " ms, maxElapsed=" << maxElapsed.count() << " ms" << endl;


    return 0;
}
