#include <QApplication>
#include "mvlc/mvlc_dev_gui.h"
#include "mvlc/mvlc_impl_factory.h"

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    qRegisterMetaType<QVector<u8>>("QVector<u8>");
    qRegisterMetaType<QVector<u32>>("QVector<u32>");
    qRegisterMetaType<FrameCheckData>("FrameCheckData");
    qRegisterMetaType<OwningPacketReadResult>("OwningPacketReadResult");
    qRegisterMetaType<EthDebugBuffer>("EthDebugBuffer");
    qRegisterMetaType<FixedSizeBuffer>("FixedSizeBuffer");

    QApplication app(argc, argv);

    // actionQuit
    auto actionQuit = new QAction("&Quit");
    actionQuit->setShortcut(QSL("Ctrl+Q"));
    actionQuit->setShortcutContext(Qt::ApplicationShortcut);

    QObject::connect(actionQuit, &QAction::triggered,
                     &app, &QApplication::quit);

    LogWidget logWindow;
    logWindow.addAction(actionQuit);

    auto mvlc_usb = std::make_unique<MVLCObject>(make_mvlc_usb());
    auto mvlc_eth = std::make_unique<MVLCObject>(make_mvlc_eth("192.168.42.2"));

    for (auto mvlc: { mvlc_usb.get(), mvlc_eth.get() })
    {
        auto poller = new MVLCNotificationPoller(*mvlc, mvlc);

        QObject::connect(poller, &MVLCNotificationPoller::stackErrorNotification,
                         mvlc, &MVLCObject::stackErrorNotification);

        poller->enablePolling();
    }

    MVLCDevGUI devGui_usb(mvlc_usb.get());
    devGui_usb.setWindowTitle("MVLC Dev GUI - USB");

    MVLCDevGUI devGui_eth(mvlc_eth.get());
    devGui_eth.setWindowTitle("MVLC Dev GUI - ETH");

    for (auto devgui: { &devGui_usb, &devGui_eth })
    {
        QObject::connect(devgui, &MVLCDevGUI::sigLogMessage,
                         &logWindow, &LogWidget::logMessage);

        auto poller = new MVLCNotificationPoller(*mvlc_usb);

        devgui->addAction(actionQuit);

        devgui->resize(1000, 960);
        devgui->show();
    }

    logWindow.resize(600, 960);
    logWindow.show();

    return app.exec();
}

