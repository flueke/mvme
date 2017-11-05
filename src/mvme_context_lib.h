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

OpenListfileResult open_listfile(MVMEContext *context, const QString &filename, u16 flags = 0);

#endif /* __MVME_CONTEXT_LIB_H__ */
