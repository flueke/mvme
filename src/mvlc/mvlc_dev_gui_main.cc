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
#include <QApplication>
#include <iostream>
#include "mvlc/mvlc_dev_gui.h"

using namespace mesytec;
using namespace mesytec::mvme_mvlc;

int main(int argc, char *argv[])
{
    qRegisterMetaType<QVector<u8>>("QVector<u8>");
    qRegisterMetaType<QVector<u32>>("QVector<u32>");
    qRegisterMetaType<FrameCheckData>("FrameCheckData");
    qRegisterMetaType<OwningPacketReadResult>("OwningPacketReadResult");
    qRegisterMetaType<EthDebugBuffer>("EthDebugBuffer");
    qRegisterMetaType<FixedSizeBuffer>("FixedSizeBuffer");

    QApplication app(argc, argv);

    if (argc < 2 || argv[1] == std::string("--help"))
    {
        std::cout << "Usage: " << argv[0] << " <mvlc hostname / ip address>" << std::endl;
        return 1;
    }

    // actionQuit
    auto actionQuit = new QAction("&Quit");
    actionQuit->setShortcut(QSL("Ctrl+Q"));
    actionQuit->setShortcutContext(Qt::ApplicationShortcut);

    QObject::connect(actionQuit, &QAction::triggered,
                     &app, &QApplication::quit);

    LogWidget logWindow;
    logWindow.addAction(actionQuit);

    auto mvlc_usb = std::make_unique<MVLCObject>(mvlc::make_mvlc_usb());
    auto mvlc_eth = std::make_unique<MVLCObject>(mvlc::make_mvlc_eth(argv[1]));

#if 0 // FIXME: error polling
    for (auto mvlc: { mvlc_usb.get(), mvlc_eth.get() })
    {
        auto poller = new MVLCNotificationPoller(*mvlc, mvlc);

        poller->enablePolling();
    }
#endif

    MVLCDevGUI devGui_usb(mvlc_usb.get());
    devGui_usb.setWindowTitle("MVLC Dev GUI - USB");

    MVLCDevGUI devGui_eth(mvlc_eth.get());
    devGui_eth.setWindowTitle("MVLC Dev GUI - ETH");

    for (auto devgui: { &devGui_usb, &devGui_eth })
    {
        QObject::connect(devgui, &MVLCDevGUI::sigLogMessage,
                         &logWindow, &LogWidget::logMessage);

        devgui->addAction(actionQuit);

        devgui->resize(1000, 960);
        devgui->show();
    }

    logWindow.resize(600, 960);
    logWindow.show();

    return app.exec();
}

