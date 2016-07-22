#ifndef UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0
#define UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0

#include <QQueue>
#include "util.h"

struct DataBuffer
{
    DataBuffer(size_t size)
        : data(new u8[size])
        , size(size)
        , used(0)
    {}

    ~DataBuffer()
    {
        delete[] data;
    }

    u16 *asU16() { return reinterpret_cast<u16 *>(data); }
    u32 *asU32() { return reinterpret_cast<u32 *>(data); }

    u8 *data;
    size_t size;
    size_t used;
};

typedef QQueue<DataBuffer *> DataBufferQueue;

#endif
