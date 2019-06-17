#include "listfile_replay_worker.h"

ListfileReplayWorker::ListfileReplayWorker(
    ThreadSafeDataBufferQueue *emptyBufferQueue,
    ThreadSafeDataBufferQueue *filledBufferQueue,
    QObject *parent)
: QObject(parent)
, m_emptyBufferQueue(emptyBufferQueue)
, m_filledBufferQueue(filledBufferQueue)
{ }

void ListfileReplayWorker::setLogger(LoggerFun logger)
{
    m_logger = logger;
}
