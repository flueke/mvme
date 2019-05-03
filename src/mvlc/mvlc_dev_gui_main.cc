#include <QApplication>
#include "mvlc/mvlc_dev_gui.h"
#include "mvlc/mvlc_impl_factory.h"

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    qRegisterMetaType<QVector<u8>>("QVector<u8>");
    qRegisterMetaType<QVector<u32>>("QVector<u32>");

    QApplication app(argc, argv);

    // actionQuit
    auto actionQuit = new QAction("&Quit");
    actionQuit->setShortcut(QSL("Ctrl+Q"));
    actionQuit->setShortcutContext(Qt::ApplicationShortcut);

    QObject::connect(actionQuit, &QAction::triggered,
                     &app, &QApplication::quit);

    LogWidget logWindow;
    logWindow.addAction(actionQuit);

    MVLCDevGUI devGui_usb(std::make_unique<MVLCObject>(make_mvlc_usb()));
    devGui_usb.setWindowTitle("MVLC Dev GUI - USB");

    MVLCDevGUI devGui_udp(std::make_unique<MVLCObject>(make_mvlc_udp("192.168.42.2")));
    devGui_udp.setWindowTitle("MVLC Dev GUI - UDP");

    for (auto devgui: { &devGui_usb, &devGui_udp })
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

