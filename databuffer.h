#ifndef UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0
#define UUID_5cab16b7_7baf_453f_a7b3_e878cfdd7bd0

#include <QQueue>

struct DataBuffer
{
    DataBuffer(size_t size)
        : data(new char[size])
        , size(size)
        , used(0)
    {}

    ~DataBuffer()
    {
        delete[] data;
    }

    uint16_t *asU16() { return reinterpret_cast<uint16_t *>(data); }
    uint32_t *asU32() { return reinterpret_cast<uint32_t *>(data); }

    char *data;
    size_t size;
    size_t used;
};

typedef QQueue<DataBuffer *> DataBufferQueue;

#endif
