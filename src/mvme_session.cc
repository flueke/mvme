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
#include "mvme_session.h"

#include <mesytec-mvlc/util/logging.h>
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

void mvme_init(const QString &appName, bool showDebugInfo)
{
    Q_INIT_RESOURCE(mvme_resources);

    register_mvme_qt_metatypes();

    QCoreApplication::setOrganizationDomain("www.mesytec.com");
    QCoreApplication::setOrganizationName("mesytec");
    QCoreApplication::setApplicationName(appName);
    QCoreApplication::setApplicationVersion(mvme_git_version());

    QLocale::setDefault(QLocale::c());

    if (showDebugInfo)
    {
        qDebug() << "prefixPath = " << QLibraryInfo::location(QLibraryInfo::PrefixPath);
        qDebug() << "librariesPaths = " << QLibraryInfo::location(QLibraryInfo::LibrariesPath);
        qDebug() << "pluginsPaths = " << QLibraryInfo::location(QLibraryInfo::PluginsPath);
        qDebug() << "mvme_git_version = " << mvme_git_version();
        qDebug() << "mvme_git_describe_version = " << mvme_git_describe_version();
        qDebug() << "BUILD_TYPE =" << BUILD_TYPE;
        qDebug() << "BUILD_CXX_FLAGS =" << BUILD_CXX_FLAGS;
    }

#ifndef NDEBUG
    spdlog::set_level(spdlog::level::debug);
    mesytec::mvlc::set_global_log_level(spdlog::level::debug);
#else
    spdlog::set_level(spdlog::level::info);
    mesytec::mvlc::set_global_log_level(spdlog::level::info);
#endif
}

void mvme_shutdown()
{
}

void register_mvme_qt_metatypes()
{
    qRegisterMetaType<DAQState>("DAQState");
    qRegisterMetaType<GlobalMode>("GlobalMode");
    qRegisterMetaType<AnalysisWorkerState>("AnalysisWorkerState");
    qRegisterMetaType<ControllerState>("ControllerState");
    qRegisterMetaType<Qt::Axis>("Qt::Axis");
    qRegisterMetaType<mesytec::mvme_mvlc::MVLCObject::State>("mesytec::mvme_mvlc::MVLCObject::State");
    qRegisterMetaType<DataBuffer>("DataBuffer");
    qRegisterMetaType<EventRecord>("EventRecord");

    qRegisterMetaType<ContainerObject *>();
    qRegisterMetaType<VMEScriptConfig *>();
    qRegisterMetaType<ModuleConfig *>();
    qRegisterMetaType<EventConfig *>();
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
}
