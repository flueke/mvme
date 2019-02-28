#ifndef __MVME_WORKSPACE_H__
#define __MVME_WORKSPACE_H__

#include <QSettings>
#include <QDir>
#include <memory>

static const char *WorkspaceIniName = "mvmeworkspace.ini";

inline std::shared_ptr<QSettings> make_workspace_settings(const QString &workspaceDirPath)
{
    if (workspaceDirPath.isEmpty())
        return {};

    QDir dir(workspaceDirPath);
    return std::make_shared<QSettings>(dir.filePath(WorkspaceIniName), QSettings::IniFormat);
}

#endif /* __MVME_WORKSPACE_H__ */
