#ifndef SRC_UTIL_QT_FS_H
#define SRC_UTIL_QT_FS_H

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QString>
#include <QTextStream>

inline QString filepath_relative_to_cwd(const QString &filepath)
{
    auto inputFilepath = QFileInfo(filepath).canonicalFilePath();
    auto curAbsPath = QDir().canonicalPath() + "/";
    //qDebug() << "inputFilepath=" << inputFilepath << "curAbsPath=" << curAbsPath;
    if (inputFilepath.startsWith(curAbsPath))
        inputFilepath.remove(0, curAbsPath.size());
    return inputFilepath;
}

// Note: error info is not exposed to the outside for the following functions.

inline QString read_text_file(const QString &fileName)
{
    QFile inFile(fileName);

    if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QTextStream inStream(&inFile);
    return inStream.readAll();
}

inline std::pair<QByteArray, QString> read_binary_file(const QString &fileName)
{
    QFile inFile(fileName);

    if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::pair<QByteArray, QString>({}, inFile.errorString());

    return std::pair<QByteArray, QString>(inFile.readAll(), {});
}

inline QJsonDocument read_json_file(const QString &fileName)
{
    auto data = read_text_file(fileName);
    auto doc(QJsonDocument::fromJson(data.toUtf8()));
    return doc;
}

#endif // SRC_UTIL_QT_FS_H
