#ifndef UTIL_H
#define UTIL_H

#include <QtDebug>

void debugOutputBuffer(uint32_t *dataBuffer, uint32_t bufferCount)
{
  for (uint32_t bufferIndex=0; bufferIndex < bufferCount; ++bufferIndex)
  {
    qDebug("%3u: %08x", bufferIndex, dataBuffer[bufferIndex]);
  }
}

#endif // UTIL_H
