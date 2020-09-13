#include "logfile_helper.h"
#include <cassert>
#include <QDebug>

namespace mesytec
{
namespace mvme
{

struct LogfileHelper::Private
{
    QDir logDir;
    unsigned maxFiles;
    QFile currentFile;
};

LogfileHelper::LogfileHelper(const QDir &logDir, unsigned maxFiles, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->logDir = logDir;
    d->maxFiles = maxFiles;

    if (d->maxFiles == 0)
        throw std::runtime_error("LogFileHelper: maxFiles is 0");
}

LogfileHelper::~LogfileHelper()
{
}

bool LogfileHelper::beginNewFile(const QString &filenamePrefix)
{
    if (filenamePrefix.isEmpty())
        return false;

    closeCurrentFile();

    // Count the number of log files in the logdir. If it exceeds maxFiles then
    // remove the oldest log files until the number of remaining files is
    // maxFiles-1.

    auto list_logfiles = [this] () -> QStringList
    {
        return d->logDir.entryList({"*.log"}, QDir::Files, QDir::Time | QDir::Reversed);
    };

    auto logfiles = list_logfiles();

    qDebug() << __PRETTY_FUNCTION__ << logfiles;

    if (static_cast<unsigned>(logfiles.size()) >= d->maxFiles)
    {
        const auto rmCount = (logfiles.size() - d->maxFiles) + 1;
        qDebug() << __PRETTY_FUNCTION__ << rmCount;
        auto rmBegin = logfiles.begin();
        const auto rmEnd = rmBegin + rmCount;

        while (rmBegin != rmEnd)
        {
            auto absfile = d->logDir.absoluteFilePath(*rmBegin++);

            qDebug() << __PRETTY_FUNCTION__ << "removing " << absfile;

            if (!QFile::remove(absfile))
                return false;
        }
    }

    assert(static_cast<unsigned>(list_logfiles().size()) <= d->maxFiles);

    auto newFilename = d->logDir.absoluteFilePath(filenamePrefix + ".log");

    d->currentFile.setFileName(newFilename);

    // TODO: append a counter suffix to the prefix if the file exists
    if (d->currentFile.exists())
        return false;

    return d->currentFile.open(QIODevice::WriteOnly);
}

bool LogfileHelper::closeCurrentFile()
{
    if (hasOpenFile())
    {
        d->currentFile.close();
        return true;
    }

    return false;
}

bool LogfileHelper::flush()
{
    return d->currentFile.flush();
}

bool LogfileHelper::logMessage(const QString &msg)
{
    if (!hasOpenFile())
        return false;

    return (d->currentFile.write(msg.toUtf8()) >= 0);
}

bool LogfileHelper::hasOpenFile() const
{
    return d->currentFile.isOpen();
}

QDir LogfileHelper::logDir() const
{
    return d->logDir;
}

QString LogfileHelper::currentFilename() const
{
    return d->currentFile.fileName();
}

QString LogfileHelper::currentAbsFilepath() const
{
    if (!hasOpenFile())
        return {};

    return d->logDir.absoluteFilePath(currentFilename());
}

unsigned LogfileHelper::maxFiles() const
{
    return d->maxFiles;
}

} // end namespace mvme
} // end namespace mesytec
