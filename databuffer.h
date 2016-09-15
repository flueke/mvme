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

    u8 *data;
    size_t size;
    size_t used;
    int type = 0;
};

typedef QQueue<DataBuffer *> DataBufferQueue;

#endif
