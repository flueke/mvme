#include <util.h>
#include <QtDebug>
#include <QTextStream>
#include <QFile>

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

    while (!input.atEnd())
    {
        u32 value;
        input >> value;

        if (input.status() == QTextStream::Ok)
        {
            ret.append(value);
        }
        else
        {
            input.resetStatus();
            char c;
            do
            {
                input >> c;
            } while (!input.atEnd() && c != '\n' && c != '\r');
        }
    }

    return ret;
}

InitList parseInitList(QTextStream &input)
{
    auto vec = parseStackFile(input);

    InitList ret;

    for (int i=0; i<vec.size()/2; ++i)
    {
        u32 addr  = vec[2*i];
        u32 value = vec[2*i+1];
        ret.push_back(qMakePair(addr, value));
    }

    return ret;
}

InitList parseInitList(const QString &input)
{
    QTextStream strm(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return parseInitList(strm);
}

QString readStringFile(const QString &filename)
{
    QString ret;
    QFile infile(filename);
    if (infile.open(QIODevice::ReadOnly))
    {
        QTextStream instream(&infile);
        ret = instream.readAll();
    }

    return ret;
}
