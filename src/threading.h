#ifndef __THREADING_H__
#define __THREADING_H__

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QMutexLocker>

template<typename T>
struct ThreadSafeQueue
{
    QQueue<T> queue;
    QMutex mutex;
    QWaitCondition wc;
};

class DataBuffer;

using ThreadSafeDataBufferQueue = ThreadSafeQueue<DataBuffer *>;

#endif /* __THREADING_H__ */
