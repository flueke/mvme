/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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

template<typename T>
void enqueue(ThreadSafeQueue<T> *tsq, T obj)
{
    QMutexLocker lock(&tsq->mutex);
    tsq->queue.enqueue(obj);
}

template<typename T>
void enqueue_and_wakeOne(ThreadSafeQueue<T> *tsq, T obj)
{
    tsq->mutex.lock();
    tsq->queue.enqueue(obj);
    tsq->mutex.unlock();
    tsq->wc.wakeOne();
}

// Dequeue operation returning a default constructed value if the queue is
// empty.
template<typename T>
T dequeue(ThreadSafeQueue<T> *tsq)
{
    QMutexLocker lock(&tsq->mutex);
    if (!tsq->queue.isEmpty())
        return tsq->queue.dequeue();
    return {};
}

// Dequeue operation waiting for the queues wait condition to be signaled in
// case the queue is empty. A default constructed value is returned in case the
// wait times out and the queue is still empty.
template<typename T>
T dequeue(ThreadSafeQueue<T> *tsq, unsigned  long wait_ms)
{
    QMutexLocker lock(&tsq->mutex);

    if (tsq->queue.isEmpty())
        tsq->wc.wait(&tsq->mutex, wait_ms);

    if (!tsq->queue.isEmpty())
        return tsq->queue.dequeue();

    return {};
}

template<typename T>
T dequeue_blocking(ThreadSafeQueue<T> *tsq)
{
    QMutexLocker lock(&tsq->mutex);

    while (tsq->queue.isEmpty())
        tsq->wc.wait(&tsq->mutex);

    return tsq->queue.dequeue();
}

template<typename T>
bool is_empty(ThreadSafeQueue<T> *tsq)
{
    QMutexLocker lock(&tsq->mutex);
    return tsq->queue.isEmpty();
}

template<typename T>
int queue_size(ThreadSafeQueue<T> *tsq)
{
    QMutexLocker lock(&tsq->mutex);
    return tsq->queue.size();
}

#endif /* __THREADING_H__ */
