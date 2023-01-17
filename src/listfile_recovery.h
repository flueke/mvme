#ifndef SRC_LISTFILE_RECOVERY_H_
#define SRC_LISTFILE_RECOVERY_H_

#include <string>
#include <QWizard>
#include "libmvme_export.h"
#include "util/typedefs.h"
#include <mesytec-mvlc/util/protected.h>

namespace mesytec::mvme::listfile_recovery
{

struct LIBMVME_EXPORT EntryFindResult
{
    size_t headerOffset;    // offset in bytes to the file header from the start of the zip file
    size_t dataStartOffset; // offset in byytes to the start of the data of the found entry
    u16 compressionType;    // MZ_COMPRESS: 0=store, 8=deflate
    std::string entryName;  // filename of the entry
};

struct LIBMVME_EXPORT RecoveryProgress
{
    size_t inputBytesRead;
    size_t inputFileSize;
    size_t outputBytesWritten;
};

EntryFindResult LIBMVME_EXPORT
    find_first_entry(const std::string &zipFilename);

RecoveryProgress LIBMVME_EXPORT
    recover_listfile(
        const std::string &inputFilename,
        const std::string &outputFilename,
        const EntryFindResult &entryInfo,
        mesytec::mvlc::Protected<RecoveryProgress> &progress
        );


class LIBMVME_EXPORT ListfileRecoveryWizard: public QWizard
{
};

}

#endif // SRC_LISTFILE_RECOVERY_H_