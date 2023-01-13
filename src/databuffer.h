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
#ifndef UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0
#define UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0

#include "util.h"
#include <QQueue>
#include <cstring>

#include <QDebug>

#define DATABUFFER_ENABLE_COPY

struct DataBuffer
{
    DataBuffer()
        : DataBuffer(0, 0, 0)
    {}

    DataBuffer(size_t sz, s32 tag = 0, u32 id = 0u)
        : data(nullptr)
        , size(0)
        , used(0)
        , id(id)
        , tag(tag)
    {
        reserve(sz);
    }

    ~DataBuffer()
    {
        delete[] data;
    }

#ifdef DATABUFFER_ENABLE_COPY
    // copy construction
    DataBuffer(const DataBuffer &other)
        : DataBuffer()
    {
        *this = other;
    }

    // copy assignment
    DataBuffer &operator=(const DataBuffer &other)
    {
        if (this != &other)
        {
            used = 0u;
            reserve(other.size);
            std::memcpy(data, other.data, other.used);
            used = other.used;
            id = other.id;
            tag = other.tag;
        }

        return *this;
    }
#else
    DataBuffer(const DataBuffer &) = delete;
    DataBuffer &operator=(const DataBuffer &) = delete;
#endif

    // move construction
    DataBuffer(DataBuffer &&other)
    {
        data = other.data;
        size = other.size;
        used = other.used;
        id = other.id;
        tag = other.tag;

        other.data = nullptr;
        other.size = 0;
        other.used = 0;
        other.id = 0;
        other.tag = 0;
    }

    // move assignment
    DataBuffer &operator=(DataBuffer &&other)
    {
        if (this != &other)
        {
            delete[] data;

            data = other.data;
            size = other.size;
            used = other.used;
            id = other.id;
            tag = other.tag;

            other.data = nullptr;
            other.size = 0;
            other.used = 0;
            other.id = 0;
            other.tag = 0;
        }

        return *this;
    }

    void reserve(size_t newSize)
    {
        // never shrink
        if (newSize <= size)
            return;

        // Allocate in terms of u32 to get the alignment right for 32-bit access.
        size_t sizeu32 = newSize/sizeof(u32) + 1;

        u8 *newData = reinterpret_cast<u8 *>(new u32[sizeu32]);
        if (used)
            std::memcpy(newData, data, used);
        delete[] data;
        data = newData;
        size = newSize; // Store the requested size in member variable
    }

    size_t free() const { return size - used; }

    u8 *asU8() { return data + used; }
    u16 *asU16() { return reinterpret_cast<u16 *>(data + used); }
    u32 *asU32() { return reinterpret_cast<u32 *>(data + used); }
    const u32 *asU32() const { return reinterpret_cast<const u32 *>(data + used); }

    s8 *asS8() { return reinterpret_cast<s8 *>(data + used); }
    s16 *asS16() { return reinterpret_cast<s16 *>(data + used); }
    s32 *asS32() { return reinterpret_cast<s32 *>(data + used); }

    char *asCharStar() { return reinterpret_cast<char *>(data + used); }

    u32 *asU32(size_t offset) { return reinterpret_cast<u32 *>(data + offset); }
    const u32 *asU32(size_t offset) const { return reinterpret_cast<const u32 *>(data + offset); }

    u32 *indexU32(size_t index)
    {
        if (index * sizeof(u32) >= used)
            throw end_of_buffer();

        return reinterpret_cast<u32 *>(data) + index;
    }

    size_t usedU32() const
    {
        return used / sizeof(u32);
    };

    template<typename T>
    T *append(const T &value)
    {
        if (free() < sizeof(T))
            throw end_of_buffer();

        auto result = reinterpret_cast<T *>(data + used);
        *result = value;
        used += sizeof(T);
        return result;
    }

    u8 *endPtr()
    {
        return data + used;
    }

    void ensureFreeSpace(size_t freeSize)
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
        auto result = new DataBuffer(opt == Deepcopy_AllocateFullSize ? size : used,
                                     tag, id);

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
    u32 id = 0u; // id value for external use (e.g. buffer number)
    s32 tag = 0; // tag allowing to distinguish buffer types
};

typedef QQueue<DataBuffer *> DataBufferQueue;

inline void move_bytes(DataBuffer &sourceBuffer, DataBuffer &destBuffer,
                       const u8 *sourceBegin, size_t bytes)
{
    assert(sourceBegin >= sourceBuffer.data);
    assert(sourceBegin + bytes <= sourceBuffer.endPtr());

    destBuffer.ensureFreeSpace(bytes);
    std::memcpy(destBuffer.endPtr(), sourceBegin, bytes);
    destBuffer.used   += bytes;
    sourceBuffer.used -= bytes;
}

#endif
