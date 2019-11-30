#include "mvme_session.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>

#include "analysis/analysis_session.h"
#include "build_info.h"
#include "git_sha1.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvme_stream_worker.h"
#include "mvlc_stream_worker.h"
#include "vme_config.h"
#include "vme_controller.h"

void mvme_init(const QString &appName)
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<GlobalMode>("GlobalMode");
    qRegisterMetaType<MVMEStreamWorkerState>("MVMEStreamWorkerState");
    qRegisterMetaType<ControllerState>("ControllerState");
    qRegisterMetaType<Qt::Axis>("Qt::Axis");
    qRegisterMetaType<mesytec::mvlc::MVLCObject::State>("mesytec::mvlc::MVLCObject::State");
    qRegisterMetaType<DataBuffer>("DataBuffer");
    qRegisterMetaType<EventRecord>("EventRecord");

    qRegisterMetaType<ContainerObject *>();
    qRegisterMetaType<VMEScriptConfig *>();
    qRegisterMetaType<ModuleConfig *>();
    qRegisterMetaType<EventConfig *>();
    qRegisterMetaType<VMEConfig *>();
    qRegisterMetaType<VMEConfig *>();

#define REG_META_VEC(T) \
    qRegisterMetaType<QVector<T>>("QVector<"#T">")

    REG_META_VEC(u8);
    REG_META_VEC(u16);
    REG_META_VEC(u32);

    REG_META_VEC(s8);
    REG_META_VEC(s16);
    REG_META_VEC(s32);

#undef REG_META_VEC

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

void mvme_shutdown()
{
}
