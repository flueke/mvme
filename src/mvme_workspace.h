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
#ifndef __MVME_WORKSPACE_H__
#define __MVME_WORKSPACE_H__

#include <QSettings>
#include <QDir>
#include <QDebug>
#include <memory>

static const char *WorkspaceIniName = "mvmeworkspace.ini";

// TODO: fix this, it's horrible. why shared_ptr? why does it require a
// workspaceDirPath? mvme does change to the workspace directory when opening
// the workspace
inline std::shared_ptr<QSettings> make_workspace_settings(const QString &workspaceDirPath)
{
    if (workspaceDirPath.isEmpty())
        return std::make_shared<QSettings>();

    QDir dir(workspaceDirPath);
    auto path = dir.filePath(WorkspaceIniName);
    auto result = std::make_shared<QSettings>(path, QSettings::IniFormat);

    return result;
}

#endif /* __MVME_WORKSPACE_H__ */
