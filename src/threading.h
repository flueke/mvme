/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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

class DataBuffer;

using ThreadSafeDataBufferQueue = ThreadSafeQueue<DataBuffer *>;

#endif /* __THREADING_H__ */
