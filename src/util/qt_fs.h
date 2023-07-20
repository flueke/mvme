#ifndef SRC_UTIL_QT_FS_H
#define SRC_UTIL_QT_FS_H

#include <QDir>
#include <QFileInfo>
#include <QString>

inline QString filepath_relative_to_cwd(const QString &filepath)
{
    auto inputFilepath = QFileInfo(filepath).canonicalFilePath();
    auto curAbsPath = QDir().canonicalPath() + "/";
    //qDebug() << "inputFilepath=" << inputFilepath << "curAbsPath=" << curAbsPath;
    if (inputFilepath.startsWith(curAbsPath))
        inputFilepath.remove(0, curAbsPath.size());
    return inputFilepath;
}

#endif // SRC_UTIL_QT_FS_H
