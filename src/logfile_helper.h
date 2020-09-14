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

class LIBMVME_EXPORT LogfileHelper: public QObject
{
    public:
        explicit LogfileHelper(const QDir &logDir, unsigned maxFiles = 10, QObject *parent = nullptr);
        ~LogfileHelper() override;

        bool beginNewFile(const QString &filenamePrefix);
        bool closeCurrentFile();
        bool flush();

        bool hasOpenFile() const;
        QDir logDir() const;
        QString currentFilename() const;
        QString currentAbsFilepath() const;
        unsigned maxFiles() const;

    public slots:
        bool logMessage(const QString &msg);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mvme
} // end namespace mesytec

#endif /* __MVME_LOGFILE_HELPER_H__ */
