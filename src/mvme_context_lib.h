#ifndef __MVME_CONTEXT_LIB_H__
#define __MVME_CONTEXT_LIB_H__

#include "mvme_listfile.h"

class MVMEContext;

struct OpenListfileResult
{
    ListFile *listfile;             // Owned by the context object
    QByteArray messages;            // messages.log if found
    QByteArray analysisBlob;        // analysis config contents
    QString analysisFilename;       // analysis filename inside the archive
};

struct OpenListfileFlags
{
    static const u16 LoadAnalysis = 1u << 0;
};

/* Important: Does not check if the current analysis is modified before loading
 * one from the listfile. Perform this check before calling this function! */
OpenListfileResult open_listfile(MVMEContext *context, const QString &filename, u16 flags = 0);

struct OpenListfileResultLowLevel
{
    std::unique_ptr<ListFile> listfile;
    QByteArray messages;                    // messages.log if found
    QByteArray analysisBlob;                // analysis config contents
    QString analysisFilename;               // analysis filename inside the archive

    OpenListfileResultLowLevel() = default;

    OpenListfileResultLowLevel(OpenListfileResultLowLevel &&) = default;
    OpenListfileResultLowLevel &operator=(OpenListfileResultLowLevel &&) = default;

    OpenListfileResultLowLevel(const OpenListfileResultLowLevel &) = delete;
    OpenListfileResultLowLevel &operator=(const OpenListfileResultLowLevel &) = delete;
};

OpenListfileResultLowLevel open_listfile(const QString &filename);

#endif /* __MVME_CONTEXT_LIB_H__ */
