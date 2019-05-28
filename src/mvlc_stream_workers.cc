#include "mvlc_stream_workers.h"

#include "mvme_context.h"

MVLC_StreamWorkerBase::MVLC_StreamWorkerBase(
    MVMEContext *context,
    ThreadSafeDataBufferQueue *freeBuffers,
    ThreadSafeDataBufferQueue *fullBuffers,
    QObject *parent)
: StreamWorkerBase(parent)
, m_context(context)
, m_freeBuffers(freeBuffers)
, m_fullBuffers(fullBuffers)
{
}
