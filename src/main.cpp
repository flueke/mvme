#include "mvme.h"
#include "util.h"
#include "vmusb_stack.h"
#include "mvme_context.h"
#include "vme_controller.h"
#include "daqconfig_tree.h"
#include "mvme_config.h"

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QSplashScreen>
#include <QTimer>

int main(int argc, char *argv[])
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<DAQState>("GlobalMode");
    qRegisterMetaType<DAQState>("ControllerState");

    QApplication app(argc, argv);

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName("mvme");
    QCoreApplication::setApplicationVersion(GIT_VERSION);

    QLocale::setDefault(QLocale::c());

    qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
    qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);

    QSplashScreen splash(QPixmap(":/mesytec-logo.png"), Qt::CustomizeWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    splash.showMessage(QSL("                                       mvme - VME Data Acquisition\n"
                           "                                © 2015-2016 mesytec GmbH & Co. KG"
                          ));
    splash.show();

    const int splashMaxTime = 3000;
    QTimer splashTimer;
    splashTimer.setInterval(splashMaxTime);
    splashTimer.setSingleShot(true);
    splashTimer.start();
    QObject::connect(&splashTimer, &QTimer::timeout, &splash, &QWidget::close);

    mvme w;
    w.show();
    w.restoreSettings();

    int ret = app.exec();

    return ret;
}
