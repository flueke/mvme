#include "mvme_session.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>

#include "mvme_stream_worker.h"
#include "vme_controller.h"

#ifdef MVME_USE_GIT_VERSION_FILE
#include "git_sha1.h"
#endif
#include "build_info.h"
#ifdef MVME_ENABLE_HDF5
#include "analysis/analysis_session.h"
#endif

void mvme_init(const QString &appName)
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<GlobalMode>("GlobalMode");
    qRegisterMetaType<MVMEStreamWorkerState>("MVMEStreamWorkerState");
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

#ifdef MVME_ENABLE_HDF5
    analysis::analysis_session_system_init();
#endif
}

void mvme_shutdown()
{
#ifdef MVME_ENABLE_HDF5
    analysis::analysis_session_system_destroy();
#endif
}
