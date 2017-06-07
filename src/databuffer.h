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
#ifndef UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0
#define UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0

#include "util.h"
#include <QQueue>
#include <cstring>

struct DataBuffer
{
    DataBuffer(size_t size, int type=0)
        : data(new u8[size])
        , size(size)
        , used(0)
        , type(type)
    {}

    ~DataBuffer()
    {
        delete[] data;
    }

    void reserve(size_t newSize)
    {
        if (newSize <= size)
            return;

        u8 *newData = new u8[newSize];
        memcpy(newData, data, used);
        delete[] data;
        data = newData;
        size = newSize;
    }

    size_t free() const { return size - used; }

    u16 *asU16() { return reinterpret_cast<u16 *>(data + used); }
    u32 *asU32() { return reinterpret_cast<u32 *>(data + used); }

    u32 *asU32(size_t offset) { return reinterpret_cast<u32 *>(data + offset); }

    void ensureCapacity(size_t freeSize)
    {
        if (freeSize > free())
        {
            reserve(used + freeSize);
            Q_ASSERT(free() >= freeSize);
        }
    }

    u8 *data;
    size_t size;
    size_t used;
    int type = 0;
};

typedef QQueue<DataBuffer *> DataBufferQueue;

#endif
