#include "mvme.h"
#include "globals.h"
#include "vme_controller.h"

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QSplashScreen>
#include <QTimer>

int main(int argc, char *argv[])
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<GlobalMode>("GlobalMode");
    qRegisterMetaType<EventProcessorState>("EventProcessorState");
    qRegisterMetaType<ControllerState>("ControllerState");

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/mesytec_icon.png"));

#if 0
    {
        QFile qssFile(":/stylesheet.qss");
        if (qssFile.open(QIODevice::ReadOnly))
        {
            auto qssData = qssFile.readAll();
            app.setStyleSheet(qssData);
        }
    }
#endif

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName("mvme");
    QCoreApplication::setApplicationVersion(GIT_VERSION);

    QLocale::setDefault(QLocale::c());

    qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
    qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);

#ifdef QT_NO_DEBUG
    QSplashScreen splash(QPixmap(":/splash-screen.png"), Qt::CustomizeWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    auto font = splash.font();
    font.setPixelSize(22);
    splash.setFont(font);
    splash.showMessage(QSL(
            "mvme - VME Data Acquisition\n"
            "© 2015-2017 mesytec GmbH & Co. KG"
            ), Qt::AlignHCenter);
    splash.show();

    const int splashMaxTime = 3000;
    QTimer splashTimer;
    splashTimer.setInterval(splashMaxTime);
    splashTimer.setSingleShot(true);
    splashTimer.start();
    QObject::connect(&splashTimer, &QTimer::timeout, &splash, &QWidget::close);
#endif

    mvme w;
    w.show();
    w.restoreSettings();

#ifdef QT_NO_DEBUG
    splash.raise();
#endif

    int ret = app.exec();

    return ret;
}
