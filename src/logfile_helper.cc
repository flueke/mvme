#include "logfile_helper.h"
#include <cassert>
#include <QDebug>

#include "util/qt_str.h"

namespace mesytec
{
namespace mvme
{

struct LogfileCountLimiter::Private
{
    QDir logDir;
    unsigned maxFiles;
    QFile currentFile;
};

LogfileCountLimiter::LogfileCountLimiter(const QDir &logDir, unsigned maxFiles, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->logDir = logDir;
    d->maxFiles = maxFiles;

    if (d->maxFiles == 0)
        throw std::runtime_error("LogFileHelper: maxFiles is 0");
}

LogfileCountLimiter::~LogfileCountLimiter()
{
}

bool LogfileCountLimiter::beginNewFile(const QString &filenamePrefix)
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

    //qDebug() << __PRETTY_FUNCTION__ << "existing logfiles:" << logfiles;

    if (static_cast<unsigned>(logfiles.size()) >= d->maxFiles)
    {
        const auto rmCount = (logfiles.size() - d->maxFiles) + 1;
        auto rmBegin = logfiles.begin();
        const auto rmEnd = rmBegin + rmCount;

        while (rmBegin != rmEnd)
        {
            auto absfile = d->logDir.absoluteFilePath(*rmBegin++);

            //qDebug() << __PRETTY_FUNCTION__ << "removing old logfile" << absfile;

            if (!QFile::remove(absfile))
                return false;
        }
    }

    assert(static_cast<unsigned>(list_logfiles().size()) <= d->maxFiles);

    auto newFilename = d->logDir.absoluteFilePath(filenamePrefix + ".log");

    d->currentFile.setFileName(newFilename);

    // TODO (maybe): append a counter suffix to the prefix if the file exists
    if (d->currentFile.exists())
        return false;

    return d->currentFile.open(QIODevice::WriteOnly | QIODevice::Text);
}

bool LogfileCountLimiter::closeCurrentFile()
{
    if (hasOpenFile())
    {
        d->currentFile.close();
        return true;
    }

    return false;
}

bool LogfileCountLimiter::flush()
{
    // If no file is open the windows QFile::flush() implementation always
    // returns true. This method should return false in this case.
    if (hasOpenFile())
        return d->currentFile.flush();
    return false;
}

bool LogfileCountLimiter::logMessage(const QString &msg)
{
    if (!hasOpenFile())
        return false;

    return (d->currentFile.write(msg.toUtf8()) >= 0);
}

bool LogfileCountLimiter::hasOpenFile() const
{
    return d->currentFile.isOpen();
}

QDir LogfileCountLimiter::logDir() const
{
    return d->logDir;
}

QString LogfileCountLimiter::currentFilename() const
{
    return d->logDir.relativeFilePath(d->currentFile.fileName());
}

QString LogfileCountLimiter::currentAbsFilepath() const
{
    if (!hasOpenFile())
        return {};

    return d->logDir.absoluteFilePath(currentFilename());
}

unsigned LogfileCountLimiter::maxFiles() const
{
    return d->maxFiles;
}

QString LogfileCountLimiter::errorString() const
{
    return d->currentFile.errorString();
}

struct LastlogHelper::Private
{
    QFile currentFile;
};

LastlogHelper::LastlogHelper(QDir logDir, const QString &logfileName,
         const QString &lastLogfileName, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    if (logDir.exists(logfileName) && logDir.exists(lastLogfileName))
    {
        if (!logDir.remove(lastLogfileName))
            throw std::runtime_error(
                ("LastlogHelper: unable to remove old logfile "
                 + logDir.absoluteFilePath(lastLogfileName)).toStdString());
    }

    assert(!logDir.exists(lastLogfileName));

    if (logDir.exists(logfileName))
    {
        if (!logDir.rename(logfileName, lastLogfileName))
            throw std::runtime_error(QSL(
                    "LastlogHelper: unable to rename last logfile from (%1 to %2)")
                .arg(logDir.absoluteFilePath(logfileName))
                .arg(logDir.absoluteFilePath(lastLogfileName)).toStdString());
    }

    assert(!logDir.exists(logfileName));

    d->currentFile.setFileName(logDir.absoluteFilePath(logfileName));

    if (!d->currentFile.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(QSL(
                "LastlogHelper: unable to open log file %1 for writing: %2")
            .arg(d->currentFile.fileName())
            .arg(d->currentFile.errorString()).toStdString());

    assert(d->currentFile.isOpen());
}

LastlogHelper::~LastlogHelper()
{
}

bool LastlogHelper::logMessage(const QString &msg)
{
    return (d->currentFile.write(msg.toUtf8()) >= 0);
}

bool LastlogHelper::flush()
{
    // If no file is open the windows QFile::flush() implementation always
    // returns true. This method should return false in this case.
    if (d->currentFile.isOpen())
        return d->currentFile.flush();
    return false;
}

} // end namespace mvme
} // end namespace mesytec
