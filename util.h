#ifndef UTIL_H
#define UTIL_H

#include <stdexcept>
#include <QVector>
#include <QList>
#include <QPair>
class QTextStream;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

void debugOutputBuffer(u32 *dataBuffer, u32 bufferCount);

QVector<u32> parseStackFile(QTextStream &input);

typedef QPair<u32, u32> RegisterSetting; // (addr, value)
typedef QList<RegisterSetting> InitList;

InitList parseInitList(QTextStream &input);

class end_of_buffer: public std::exception {};

struct BufferIterator
{
    enum Alignment { Align16, Align32 };

    BufferIterator(u8 *data, size_t size, Alignment alignment)
        : data(data)
        , buffp(data)
        , endp(data + size)
        , size(size)
        , alignment(alignment)
    {}

    u8 *data;
    u8 *buffp;
    u8 *endp;
    size_t size;
    Alignment alignment;

    inline bool align32() const { return alignment == Align32; }

    inline u8 extractU8()
    {
        if (buffp + sizeof(u8) > endp)
            throw end_of_buffer();

        u8 ret = *buffp;
        buffp += sizeof(u8);
        return ret;
    }

    inline u16 extractU16()
    {
        if (buffp + sizeof(u16) > endp)
            throw end_of_buffer();

        u32 ret = *reinterpret_cast<u16 *>(buffp);
        buffp += sizeof(u16);
        return ret;
    }

    inline u32 extractU32()
    {
        if (buffp + sizeof(u32) > endp)
            throw end_of_buffer();

        u32 ret = *reinterpret_cast<u32 *>(buffp);
        buffp += sizeof(u32);
        return ret;
    }

    inline u32 extractWord()
    {
        return align32() ? extractU32() : extractU16();
    }

    inline u8 extractByte()
    {
        return extractU8();
    }

    inline u16 extractShortword()
    {
        return extractU16();
    }

    inline u32 extractLongword()
    {
        return extractU32();
    }

    inline u16 peekU16() const
    {
        if (buffp + sizeof(u16) > endp)
            throw end_of_buffer();

        u32 ret = *reinterpret_cast<u16 *>(buffp);
        return ret;
    }

    inline u32 peekU32() const
    {
        if (buffp + sizeof(u32) > endp)
            throw end_of_buffer();

        u32 ret = *reinterpret_cast<u32 *>(buffp);
        return ret;
    }

    inline u32 peekWord() const
    {
        return align32() ? peekU32() : peekU16();
    }

    inline u32 bytesLeft() const
    {
        return endp - buffp;
    }

    inline u32 wordsLeft() const
    {
        return bytesLeft() / (align32() ? sizeof(u32) : sizeof(u16));
    }

    inline u32 shortwordsLeft() const
    {
        return bytesLeft() / sizeof(u16);
    }

    inline u32 longwordsLeft() const
    {
        return bytesLeft() / sizeof(u32);
    }

    inline u16 *asU16() { return reinterpret_cast<u16 *>(buffp); }
    inline u32 *asU32() { return reinterpret_cast<u32 *>(buffp); }

    inline void skip(size_t width, size_t count)
    {
        buffp += width * count;
        if (buffp > endp)
            buffp = endp;
    }
    
    inline bool atEnd() const { return buffp == endp; }
};


#endif // UTIL_H
