#include <util.h>
#include <QtDebug>

void debugOutputBuffer(u32 *dataBuffer, u32 bufferCount)
{
  for (u32 bufferIndex=0; bufferIndex < bufferCount; ++bufferIndex)
  {
    qDebug("%3u: %08x", bufferIndex, dataBuffer[bufferIndex]);
  }
}

QVector<u32> parseStackFile(QTextStream &input)
{
    QVector<u32> ret;
    char c;
    u32 value;

    while (!input.atEnd())
    {
        input >> value;

        if (input.status() == QTextStream::Ok)
        {
            ret.append(value);
        }
        else
        {
            input.resetStatus();
            do
            {
                input >> c;
            } while (!input.atEnd() && c != '\n' && c != '\r');
        }
    }

    return ret;
}
