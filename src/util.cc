#include "util.h"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QtDebug>
#include <QTextStream>
#include <QJsonDocument>
#include <QMessageBox>

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

QString TemplateLoader::getTemplatePath()
{
    if (m_templatePath.isEmpty())
    {
        QStringList templatePaths;
        templatePaths << QDir::currentPath() + "/templates";
        templatePaths << QCoreApplication::applicationDirPath() + "/templates";

        for (auto testPath: templatePaths)
        {
            if (QFileInfo(testPath).exists())
            {
                m_templatePath = testPath;
                emit logMessage(QString("Found template path \"%1\"").arg(m_templatePath));
                break;
            }
        }

        if (m_templatePath.isEmpty())
        {
            emit logMessage(QSL("No template path found. Tried ") + templatePaths.join(", "));
        }
    }
    return m_templatePath;
}

QString TemplateLoader::readTemplate(const QString &name)
{
    auto templatePath = getTemplatePath();
    if (templatePath.isEmpty())
        return QString();

    auto filePath = templatePath + '/' + name;

    QFileInfo fi(filePath);

    if (!fi.exists())
    {
        emit logMessage(QString("Template file %1 not found").arg(name));
        return QString();
    }

    if (!fi.isReadable())
    {
        emit logMessage(QString("Could not read template file %1").arg(name));
        return QString();
    }

    emit logMessage(QString("Reading template file %1").arg(name));

    return readStringFile(filePath);
}

QJsonDocument gui_read_json_file(const QString &fileName)
{
    QFile inFile(fileName);

    if (!inFile.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(0, QSL("Error"), QString("Error reading from %1").arg(fileName));
        return QJsonDocument();
    }

    auto data = inFile.readAll();

    if (data.isEmpty())
        return QJsonDocument();

    QJsonParseError parseError;
    QJsonDocument doc(QJsonDocument::fromJson(data, &parseError));

    if (parseError.error != QJsonParseError::NoError)
    {
        QMessageBox::critical(0, "Error", QString("Error reading from %1: %2 at offset %3")
                              .arg(fileName)
                              .arg(parseError.errorString())
                              .arg(parseError.offset)
                             );
    }
    return doc;
}

bool gui_write_json_file(const QString &fileName, const QJsonDocument &doc)
{
    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, QSL("Error"), QString("Error opening %1 for writing").arg(fileName));
        return false;
    }

    if (outFile.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, QSL("Error"), QString("Error writing to %1").arg(fileName));
        return false;
    }

    return true;
}

QPair<double, QString> byte_unit(size_t inBytes)
{
    QStringList units = { "B", "kB", "MB", "GB", "TB" };
    double bytes = inBytes;

    int power = std::floor(((bytes > 0.0) ? std::log2(bytes) : 0.0) / std::log2(1024.0));
    power = std::min(power, units.size() - 1);
    bytes /= std::pow(1024.0, power);

    return qMakePair(bytes, units[power]);
}

void logBuffer(BufferIterator iter, std::function<void (const QString &)> loggerFun)
{
    static const u32 wordsPerRow = 8;
    static const u32 berrMarker  = 0xffffffff;
    static const u32 EoMMarker   = 0x87654321;
    QString strbuf;

    while (iter.longwordsLeft())
    {
        strbuf.clear();
        u32 nWords = std::min(wordsPerRow, iter.longwordsLeft());

        for (u32 i=0; i<nWords; ++i)
        {
            u32 currentWord = iter.extractU32();

            if (currentWord == berrMarker)
            {
                strbuf += QString(QSL("BERRMarker "));
            }
            else if (currentWord == EoMMarker)
            {
                strbuf += QString(QSL(" EndMarker "));
            }
            else
            {
                strbuf += QString("0x%1 ").arg(currentWord, 8, 16, QLatin1Char('0'));
            }
        }

        loggerFun(strbuf);
    }
}
