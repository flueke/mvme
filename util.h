#ifndef UTIL_H
#define UTIL_H

#include <cstdint>
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

#endif // UTIL_H
