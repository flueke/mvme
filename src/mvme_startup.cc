#include "mvme_startup.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>

#include "globals.h"
#include "vme_controller.h"

#ifdef MVME_USE_GIT_VERSION_FILE
#include "git_sha1.h"
#endif
#include "build_info.h"

void mvme_basic_init(const QString &appName)
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<GlobalMode>("GlobalMode");
    qRegisterMetaType<EventProcessorState>("EventProcessorState");
    qRegisterMetaType<ControllerState>("ControllerState");

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName(appName);
    QCoreApplication::setApplicationVersion(GIT_VERSION);

    QLocale::setDefault(QLocale::c());

    qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
    qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);
    qDebug() << "GIT_VERSION =" << GIT_VERSION;
    qDebug() << "BUILD_TYPE =" << BUILD_TYPE;
    qDebug() << "BUILD_CXX_FLAGS =" << BUILD_CXX_FLAGS;
}
