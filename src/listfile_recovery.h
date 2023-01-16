#ifndef SRC_LISTFILE_RECOVERY_H_
#define SRC_LISTFILE_RECOVERY_H_

#include <atomic>
#include <QString>
#include <QWizard>
#include "libmvme_export.h"
#include "util/typedefs.h"

namespace mesytec::mvme::listfile_recovery
{

struct EntryInfo
{
    size_t startOffset;     // offset in bytes to the file header from the start of the zip file
    u16 compressionType;    // MZ_COMPRESS: 0=store, 8=deflate
    std::string entryName;  // filename of the entry
};

EntrySearchResult LIBMVME_EXPORT
    find_first_entry(const QString &zipFilename);

struct RecoveryProgress
{
    size_t bytesProcessed;
    size_t totalBytes;
};

struct RecoveryResult
{
    std::error_code ec;
    size_t bytesRead;
    size_t bytesWritten;
};


RecoverResult LIBMVME_EXPORT
    recover_listfile(const QString &inputFilename, const QString &outputFilename, const EntryInfo &entryInfo, std::atomic<RecoveryProgress> &progress);


class ListfileRecoveryWizard: public QWizard
{
};

}

#endif // SRC_LISTFILE_RECOVERY_H_