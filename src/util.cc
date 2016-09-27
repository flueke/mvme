#include "util.h"
#include <QtDebug>
#include <QTextStream>
#include <QFile>
#include <QCloseEvent>

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

QVector<u32> parseStackFile(const QString &input)
{
    QTextStream strm(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return parseStackFile(strm);
}

RegisterList parseRegisterList(QTextStream &input, u32 baseAddress)
{
    RegisterList result;

    while (true)
    {
        QString line = input.readLine().simplified();

        if (line.isNull())
            break;

        if (line.startsWith('#'))
            continue;

        auto parts = line.splitRef(' ', QString::SkipEmptyParts);

        if (parts.size() < 2)
            continue;

        bool ok;

        u32 address = parts[0].toULong(&ok, 0);

        if (!ok)
            continue;

        QVariant value;

        u32 intValue = parts[1].toULong(&ok, 0);

        if (ok)
        {
            value = QVariant(intValue);
        }
        else
        {
            float floatValue = parts[1].toFloat(&ok);
            if (ok)
            {
                value = QVariant(floatValue);
            }
        }

        if (value.isValid())
        {
            result.push_back(qMakePair(address + baseAddress, value));
        }
    }

    return result;
}

RegisterList parseRegisterList(const QString &input, u32 baseAddress)
{
    QTextStream strm(const_cast<QString *>(&input), QIODevice::ReadOnly);
    return parseRegisterList(strm, baseAddress);
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

static QString registerSettingToString(const RegisterSetting &rs)
{
    if (isFloat(rs.second))
    {
        return QString("0x%1 -> %2 (float)")
            .arg(rs.first, 8, 16, QLatin1Char('0'))
            .arg(rs.second.toFloat());
    }

    return QString("0x%1 -> 0x%2")
        .arg(rs.first, 8, 16, QLatin1Char('0'))
        .arg(rs.second.toUInt(), 4, 16, QLatin1Char('0'));
}

QString toString(const RegisterList &registerList)
{
    QString result;
    QTextStream stream(&result);
    stream << qSetPadChar('0') << hex;

    for (auto pair: registerList)
    {
        stream << registerSettingToString(pair) << endl;
    }
    return result;
}

QStringList toStringList(const RegisterList &registerList)
{
    QStringList ret;
    for (auto rs: registerList)
    {
        ret << registerSettingToString(rs);
    }
    return ret;
}

QString makeDurationString(qint64 durationSeconds)
{
    int seconds = durationSeconds % 60;
    durationSeconds /= 60;
    int minutes = durationSeconds % 60;
    durationSeconds /= 60;
    int hours = durationSeconds;
    QString durationString;
    durationString.sprintf("%02d:%02d:%02d", hours, minutes, seconds);
    return durationString;
}

MVMEWidget::MVMEWidget(QWidget *parent)
    : QWidget(parent)
{}

void MVMEWidget::closeEvent(QCloseEvent *event)
{
    event->accept();
    emit aboutToClose();
}
