#ifndef __MVME_LOGFILE_HELPER_H__
#define __MVME_LOGFILE_HELPER_H__

#include <QObject>
#include <QString>
#include <QDir>
#include <memory>

#include "libmvme_export.h"

namespace mesytec
{
namespace mvme
{

// The LogfileCountLimiter class is used to keep a limited number of logfiles in a
// specified log directory. When a new logfile is created via beginNewFile()
// the number of existing files in the log directory is checked. If it exceeds
// the maximum number of logs to keep the oldest (by filesystem modification
// time) logfiles are deleted until maxFiles-1 files are left. Then the new
// file is created.
// Message can be logged using the logMessage() slot. The string is converted
// to utf8 and written to the current logfile. If no file is open the message
// is discarded and logMessage() returns false;
class LIBMVME_EXPORT LogfileCountLimiter: public QObject
{
    public:
        // Creates a new LogfileCountLimiter instance. Throws std::runtime_error if maxFiles is 0.
        explicit LogfileCountLimiter(const QDir &logDir, unsigned maxFiles = 10, QObject *parent = nullptr);
        ~LogfileCountLimiter() override;

        // Opens a new logfile. Old logfiles are deleted if maxFiles is
        // exceeded. The new filename is (filenamePrefix + ".log").
        // Note: Currently no attempt is made to find a unique filename in case
        // the file already exists. Instead the existing logfile will be truncated.
        bool beginNewFile(const QString &filenamePrefix);

        // Closes the current logfile.
        bool closeCurrentFile();

        // Calls QFile::flush() on the current logfile.
        bool flush();

        bool hasOpenFile() const;
        QDir logDir() const;
        // Returns the current logfile name without any path components.
        QString currentFilename() const;
        // Returns the absolute filepath of the current logfile.
        QString currentAbsFilepath() const;
        unsigned maxFiles() const;
        // Returns the errorString() result of the internal QFile instance.
        QString errorString() const;

    public slots:
        // Writes the result of msg.toUtf8() to the current logfile.
        // Returns false if no logfile is open or a write error occured.
        bool logMessage(const QString &msg);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mvme
} // end namespace mesytec

#endif /* __MVME_LOGFILE_HELPER_H__ */
