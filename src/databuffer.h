/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0
#define UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0

#include "util.h"
#include <QQueue>
#include <cstring>

struct DataBuffer
{
    DataBuffer(size_t sz, u64 id = 0u)
        : data(nullptr)
        , size(sz)
        , used(0)
        , id(id)
    {
        if (size > 0)
        {
            // Allocate in terms of u32 to get the alignment right for 32-bit access.
            size_t sizeu32 = size/sizeof(u32) + 1;
            data = reinterpret_cast<u8 *>(new u32[sizeu32]);

            assert(sizeu32 * sizeof(u32) >= sz);

            size = sz; // Store requested size in member variable
        }
    }

    ~DataBuffer()
    {
        delete[] data;
    }

    void reserve(size_t newSize)
    {
        // never shrink
        if (newSize <= size)
            return;

        size_t sizeu32 = newSize/sizeof(u32) + 1;

        u8 *newData = reinterpret_cast<u8 *>(new u32[sizeu32]);
        std::memcpy(newData, data, used);
        delete[] data;
        data = newData;
        size = sizeu32 * sizeof(u32);
    }

    size_t free() const { return size - used; }

    u8 *asU8() { return data + used; }
    u16 *asU16() { return reinterpret_cast<u16 *>(data + used); }
    u32 *asU32() { return reinterpret_cast<u32 *>(data + used); }

    u32 *asU32(size_t offset) { return reinterpret_cast<u32 *>(data + offset); }

    u32 *indexU32(size_t index)
    {
        if (index * sizeof(u32) >= used)
            throw end_of_buffer();

        return reinterpret_cast<u32 *>(data) + index;
    }

    void ensureCapacity(size_t freeSize)
    {
        if (freeSize > free())
        {
            reserve(used + freeSize);
            Q_ASSERT(free() >= freeSize);
        }
    }

    enum DeepcopyOptions
    {
        Deepcopy_AllocateFullSize,
        Deepcopy_AllocateUsedSize
    };

    DataBuffer *deepcopy(DeepcopyOptions opt = Deepcopy_AllocateFullSize)
    {
        auto result = new DataBuffer(opt == Deepcopy_AllocateFullSize ? size : used, id);

        if (used)
        {
            std::memcpy(result->data, data, used);
            result->used = used;
        }

        return result;
    }

    u8 *data;
    size_t size; // size in bytes
    size_t used; // bytes used
    u64 id = 0u; // id value for external use

    private:
        DataBuffer(const DataBuffer &);
        DataBuffer &operator=(const DataBuffer &);
};

typedef QQueue<DataBuffer *> DataBufferQueue;

#endif
