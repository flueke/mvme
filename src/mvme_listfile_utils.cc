/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvme_listfile_utils.h"

#include <array>
#include <QDebug>
#include <QJsonDocument>
#include <QtMath>
#include <QElapsedTimer>

#include <quazip.h>
#include <quazipfile.h>

#include "globals.h"
#include "threading.h"
#include "util_zip.h"
#include "vme_config.h"
#include "vme_config_json_schema_updates.h"
#include "vme_config_util.h"


#define LISTFILE_VERBOSE 0

using namespace mesytec;
using namespace ListfileSections;

void dump_mvme_buffer(QTextStream &out, const DataBuffer *eventBuffer,
                      const ListfileConstants &lfc,  bool dumpData)
{
    BufferIterator iter(eventBuffer->data, eventBuffer->used, BufferIterator::Align32);

    while (iter.longwordsLeft())
    {
        u32 sectionHeader = iter.extractU32();
        int sectionType   = (sectionHeader & lfc.SectionTypeMask) >> lfc.SectionTypeShift;
        u32 sectionSize   = (sectionHeader & lfc.SectionSizeMask) >> lfc.SectionSizeShift;

        out << "eventBuffer: " << eventBuffer << ", used=" << eventBuffer->used
            << ", size=" << eventBuffer->size
            << endl;

        out << QStringLiteral("sectionHeader=0x%1, sectionType=%2, sectionSize=%3")
            .arg(sectionHeader, 8, 16, QLatin1Char('0'))
            .arg(sectionType)
            .arg(sectionSize);

        out << endl;

        switch (sectionType)
        {
            case SectionType_Config:
                {
                    out << "Config section of size " << sectionSize << endl;
                    iter.skip(sectionSize * sizeof(u32));
                } break;

            case SectionType_Event:
                {
                    u32 eventType = (sectionHeader & lfc.EventIndexMask) >> lfc.EventIndexShift;

                    out << QStringLiteral("Event section: eventHeader=0x%1, eventType=%2, eventSize=%3")
                        .arg(sectionHeader, 8, 16, QLatin1Char('0'))
                        .arg(eventType)
                        .arg(sectionSize);

                    out << endl;

                    u32 wordsLeft = sectionSize;

                    while (wordsLeft > 1)
                    {
                        u32 subEventHeader = iter.extractU32();
                        --wordsLeft;
                        u32 moduleType = (subEventHeader & lfc.ModuleTypeMask) >> lfc.ModuleTypeShift;
                        u32 subEventSize = (subEventHeader & lfc.ModuleDataSizeMask) >> lfc.ModuleDataSizeShift;

                        out << QStringLiteral("  subEventHeader=0x%1, moduleType=%2, subEventSize=%3")
                            .arg(subEventHeader, 8, 16, QLatin1Char('0'))
                            .arg(moduleType)
                            .arg(subEventSize);
                        out << endl;

                        for (u32 i=0; i<subEventSize; ++i)
                        {
                            u32 subEventData = iter.extractU32();
                            if (dumpData)
                            {
                                out << QStringLiteral("    %1 = 0x%2")
                                    .arg(i)
                                    .arg(subEventData, 8, 16, QLatin1Char('0'));
                                out << endl;
                            }
                        }
                        wordsLeft -= subEventSize;
                    }

                    u32 eventEndMarker = iter.extractU32();
                    out << QStringLiteral("   eventEndMarker=0x%1")
                        .arg(eventEndMarker, 8, 16, QLatin1Char('0'));
                    out << endl;
                } break;

            default:
                {
                    out << "Warning: Unknown section type " << sectionType
                        << " of size " << sectionSize
                        << ", skipping"
                        << endl;
                    iter.skip(sectionSize * sizeof(u32));
                } break;
        }
    }
}

ListFile::ListFile(QIODevice *input)
    : m_input(input)
{
}

bool ListFile::open()
{
    /* Tries to read the FourCC ("MVME") and the 4-byte version number. The
     * very first listfiles did not have this preamble so it's not an error if
     * the FourCC does not match.
     *
     * In both cases (version 0 or version > 0) a complete preamble is stored
     * in m_preambleBuffer. */

    m_fileVersion = 0;
    const auto &lfc = listfile_constants();
    const char *toCompare = lfc.FourCC;
    const size_t bytesToRead = 4;
    char fourCC[bytesToRead] = {};

    if (!m_input->isOpen())
    {
        if (!m_input->open(QIODevice::ReadOnly))
            return false;
    }
    else
    {
        seek_in_file(m_input, 0);
    }

    qint64 bytesRead = m_input->read(reinterpret_cast<char *>(fourCC), bytesToRead);

    //qDebug() << "read fourCC:" << QString::fromLatin1(fourCC, bytesToRead);

    if (bytesRead == bytesToRead
        && std::strncmp(reinterpret_cast<char *>(fourCC), toCompare, bytesToRead) == 0)
    {
        u32 version;
        bytesRead = m_input->read(reinterpret_cast<char *>(&version), sizeof(version));

        if (bytesRead == sizeof(version))
        {
            m_fileVersion = version;
        }

        qDebug() << "read listfile version" << m_fileVersion;

        u8 *firstByte = reinterpret_cast<u8 *>(fourCC);
        u8 *lastByte = reinterpret_cast<u8 *>(fourCC) + bytesToRead;

        for (u8 *c = firstByte; c < lastByte; c++)
        {
            m_preambleBuffer.push_back(*c);
        }

        firstByte = reinterpret_cast<u8 *>(&version);
        lastByte = reinterpret_cast<u8 *>(&version) + sizeof(version);

        for (u8 *c = firstByte; c < lastByte; c++)
        {
            m_preambleBuffer.push_back(*c);
        }
    }
    else
    {
        m_preambleBuffer = { 'M', 'V', 'M', 'E', 0, 0, 0, 0 };
    }

    seekToFirstSection();
    m_sectionHeaderBuffer = 0;

    qDebug() << "detected listfile version" << m_fileVersion;

    return true;
}

qint64 ListFile::size() const
{
    return m_input->size();
}

QString ListFile::getFileName() const
{
    if (auto inFile = qobject_cast<QFile *>(m_input))
    {
        return inFile->fileName();
    }
    else if (auto inZipFile = qobject_cast<QuaZipFile *>(m_input))
    {
        return inZipFile->getZipName();
    }

    InvalidCodePath;

    return QString();
}

QString ListFile::getFullName() const
{
    if (auto inFile = qobject_cast<QFile *>(m_input))
    {
        return inFile->fileName();
    }
    else if (auto inZipFile = qobject_cast<QuaZipFile *>(m_input))
    {
        return inZipFile->getZipName() + QSL(":/") + inZipFile->getFileName();
    }

    InvalidCodePath;

    return QString();
}

namespace
{
QJsonObject read_vme_config(QIODevice &m_file, const ListfileConstants &lfc)
{
    qint64 savedPos = m_file.pos();

    seek_in_file(&m_file, lfc.FirstSectionOffset);

    QByteArray configData;

    while (true)
    {
        u32 sectionHeader = 0;

        if (m_file.read((char *)&sectionHeader, sizeof(sectionHeader)) != sizeof(sectionHeader))
            break;

        int sectionType  = (sectionHeader & lfc.SectionTypeMask) >> lfc.SectionTypeShift;
        u32 sectionWords = (sectionHeader & lfc.SectionSizeMask) >> lfc.SectionSizeShift;

        //qDebug() << "sectionType" << sectionType << ", sectionWords" << sectionWords;

        if (sectionType != SectionType_Config)
            break;

        if (sectionWords == 0)
            break;

        u32 sectionSize = sectionWords * sizeof(u32);

        QByteArray data = m_file.read(sectionSize);
        configData.append(data);
    }

    seek_in_file(&m_file, savedPos);

    QJsonParseError parseError; // TODO: make parse error message available to the user
    auto m_configJson = QJsonDocument::fromJson(configData, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qDebug() << "Parse error: " << parseError.errorString();
    }

    if (!m_configJson.isEmpty())
    {
        //qDebug() << "listfile config json:" << m_configJson.toJson();
        auto result = m_configJson.object();
        // Note: This check is for compatibilty with older listfiles.
        if (result.contains(QSL("DAQConfig")))
        {
            return result.value("DAQConfig").toObject();
        }
        return m_configJson.object();
    }

    return QJsonObject();
}
}

QJsonObject ListFile::getVMEConfigJSON()
{
    if (m_configJson.isEmpty())
    {
        m_configJson = read_vme_config(*m_input, listfile_constants(m_fileVersion));
    }

    return m_configJson;
}

bool ListFile::seekToFirstSection()
{
    auto offset = listfile_constants(m_fileVersion).FirstSectionOffset;

    return seek(offset);
}

bool ListFile::seek(qint64 pos)
{
    qDebug() << m_input << getFileName() << m_input->isOpen();
    // Reset the currently saved sectionHeader on any seek.
    m_sectionHeaderBuffer = 0;
    return seek_in_file(m_input, pos);
}

bool read_next_section(QIODevice &m_file, DataBuffer *buffer, u32 *savedSectionHeader,
                       const ListfileConstants &lfc)
{
    Q_ASSERT(savedSectionHeader);

    u32 sectionHeader = *savedSectionHeader;

    if (sectionHeader == 0)
    {
        // If 0 was passed in there was no previous section header so we have to read one.
        if (m_file.read((char *)&sectionHeader, sizeof(u32)) != sizeof(u32))
            return false;
    }
    else
    {
        // Reset the saved header as we're reading that section now.
        *savedSectionHeader = 0;
    }

    u32 sectionWords = (sectionHeader & lfc.SectionSizeMask) >> lfc.SectionSizeShift;
    qint64 bytesToRead = sectionWords * sizeof(u32);

    buffer->used = 0;

    if (bytesToRead == 0)
        return true;

    buffer->reserve(bytesToRead + sizeof(u32));
    *(buffer->asU32()) = sectionHeader;
    buffer->used += sizeof(sectionHeader);

    if (m_file.read((char *)(buffer->data + buffer->used), bytesToRead) != bytesToRead)
        return false;

    buffer->used += bytesToRead;
    return true;
}

bool ListFile::readNextSection(DataBuffer *buffer)
{
    const auto &lfc = listfile_constants(m_fileVersion);

    return read_next_section(*m_input, buffer, &m_sectionHeaderBuffer, lfc);
}

s32 read_sections_into_buffer(QIODevice &m_file, DataBuffer *buffer, u32 *savedSectionHeader,
                              const ListfileConstants &lfc)
{
    Q_ASSERT(savedSectionHeader);

#if LISTFILE_VERBOSE
    qDebug() << __PRETTY_FUNCTION__ << "savedSectionHeader =" << QString::number(*savedSectionHeader, 16);
#endif

    s32 sectionsRead = 0;

    while (!m_file.atEnd() && buffer->free() >= sizeof(u32))
    {
        u32 sectionHeader = *savedSectionHeader;

        if (sectionHeader == 0)
        {
            // If 0 was passed in there was no previous section header so we have to read one.
            if (m_file.read((char *)&sectionHeader, sizeof(u32)) != sizeof(u32))
            {
                return -1;
            }
#if LISTFILE_VERBOSE
            qDebug() << "new sectionHeader =" << QString::number(sectionHeader, 16);
#endif
        }

        u32 sectionWords = (sectionHeader & lfc.SectionSizeMask) >> lfc.SectionSizeShift;

        qint64 bytesToRead = sectionWords * sizeof(u32);

        // Check if there's enough free space in the buffer for the section
        // contents plus the section header.
        if ((qint64)buffer->free() < bytesToRead + (qint64)sizeof(u32))
        {
            // Not enough space. Store the sectionHeader in *savedSectionHeader
            // and break out of the loop.
#if LISTFILE_VERBOSE
            qDebug() << "not enough space in buffer. saving section header" << QString::number(sectionHeader, 16);
#endif
            *savedSectionHeader = sectionHeader;
            break;
        }
        else
        {
            if (*savedSectionHeader != 0)
            {
#if LISTFILE_VERBOSE
                qDebug() << "got enough space in buffer. reseting saved section header to 0";
#endif
            }
            *savedSectionHeader = 0;
        }

        if (((sectionHeader & lfc.SectionTypeMask) >> lfc.SectionTypeShift) == ListfileSections::SectionType_End)
        {
            qDebug() << __PRETTY_FUNCTION__ << "read End section into buffer";
        }

        *(buffer->asU32()) = sectionHeader;
        buffer->used += sizeof(sectionHeader);

        if (m_file.read((char *)(buffer->data + buffer->used), bytesToRead) != bytesToRead)
            return -1;

        buffer->used += bytesToRead;
        ++sectionsRead;
    }

    return sectionsRead;
}

s32 ListFile::readSectionsIntoBuffer(DataBuffer *buffer)
{
    const auto &lfc = listfile_constants(m_fileVersion);

    return read_sections_into_buffer(*m_input, buffer, &m_sectionHeaderBuffer, lfc);
}

//
// ListFileWriter
//
ListFileWriter::ListFileWriter(QObject *parent)
    : QObject(parent)
    , m_out(nullptr)
{ }

ListFileWriter::ListFileWriter(QIODevice *outputDevice, QObject *parent)
    : QObject(parent)
    , m_out(outputDevice)
{ }

void ListFileWriter::setOutputDevice(QIODevice *device)
{
    if (m_out != device)
    {
        m_out = device;
        m_bytesWritten = 0;
    }
}

namespace
{

template <typename T, std::size_t N>
constexpr std::size_t countof(T const (&)[N]) noexcept
{
    return N;
}

}

bool ListFileWriter::writePreamble()
{
    const auto &lfc = listfile_constants();

    if (m_out->write(lfc.FourCC, countof(lfc.FourCC)) != static_cast<qint64>(countof(lfc.FourCC)))
        return false;

    u32 fileVersion = lfc.Version;

    if (m_out->write(reinterpret_cast<const char *>(&fileVersion), sizeof(fileVersion))
        != sizeof(fileVersion))
    {
        return false;
    }

    return true;
}

bool ListFileWriter::writeConfig(const VMEConfig *vmeConfig)
{
    auto doc = mvme::vme_config::serialize_vme_config_to_json_document(*vmeConfig);
    return writeConfig(doc.toJson());
}

bool ListFileWriter::writeConfig(QByteArray contents)
{
    const auto &lfc = listfile_constants();

    while (contents.size() % sizeof(u32))
    {
        contents.append(' ');
    }

    u32 configSize = static_cast<u32>(contents.size());
    int configSections = qCeil((float)configSize / (float)lfc.SectionMaxSize);

    DataBuffer localBuffer(configSections * lfc.SectionMaxSize // space for all config sections
                           + configSections * sizeof(u32));    // space for headers

    DataBuffer *buffer = &localBuffer;

    u8 *bufferP = buffer->data;
    const char *configP = contents.constData();

    while (configSections--)
    {
        u32 *sectionHeader = (u32 *)bufferP;
        bufferP += sizeof(u32);
        *sectionHeader = (SectionType_Config << lfc.SectionTypeShift) & lfc.SectionTypeMask;
        int sectionBytes = qMin(configSize, lfc.SectionMaxSize);
        int sectionWords = sectionBytes / sizeof(u32);
        *sectionHeader |= (sectionWords << lfc.SectionSizeShift) & lfc.SectionSizeMask;

        memcpy(bufferP, configP, sectionBytes);
        bufferP += sectionBytes;
        configP += sectionBytes;
        configSize -= sectionBytes;
    }

    buffer->used = bufferP - buffer->data;

    if (m_out->write((const char *)buffer->data, buffer->used) != (qint64)buffer->used)
        return false;

    m_bytesWritten += buffer->used;

    QTextStream out(stdout);
    dump_mvme_buffer(out, buffer, lfc, false);

    return true;
}

bool ListFileWriter::writeBuffer(const char *buffer, size_t size)
{
    qint64 written = m_out->write(buffer, size);
    if (written == static_cast<qint64>(size))
    {
        m_bytesWritten += static_cast<u64>(written);
        return true;
    }
    return false;
}

bool ListFileWriter::writeBuffer(const DataBuffer &buffer)
{
    return writeBuffer(reinterpret_cast<const char *>(buffer.data), buffer.used);
}

// Writes a section with the given sectionType with the given contents. The
// contents are padded with spaces to the next 32-bit boundary.
bool ListFileWriter::writeStringSection(SectionType sectionType,
                                        const QByteArray &contents)
{
    const auto &lfc = listfile_constants();

    u32 contentsBytes = contents.size();
    u32 paddingBytes = sizeof(u32) - (contentsBytes % sizeof(u32));
    assert(paddingBytes <= 3);

    contentsBytes += paddingBytes;
    assert(contentsBytes % sizeof(u32) == 0);

    u32 sectionWords = contentsBytes / sizeof(u32);

    assert(sectionWords * sizeof(u32) == contentsBytes);
    assert(contentsBytes <= lfc.SectionMaxSize);

    if (contentsBytes > lfc.SectionMaxSize)
        return false;

    u32 header = (sectionType << lfc.SectionTypeShift) & lfc.SectionTypeMask;
    header |= (sectionWords << lfc.SectionSizeShift) & lfc.SectionSizeMask;

    if (m_out->write((const char *)&header, sizeof(header)) != sizeof(header))
        return false;

    m_bytesWritten += sizeof(header);

    if (m_out->write(contents.data(), contents.size()) != contents.size())
        return false;

    m_bytesWritten += contents.size();

    if (paddingBytes > 0)
    {
        static const std::array<char, 3> paddingData = {{ ' ', ' ', ' ' }};

        if (m_out->write(paddingData.data(), paddingBytes) != paddingBytes)
            return false;

        m_bytesWritten += paddingBytes;
    }

    return true;
}

bool ListFileWriter::writeTimetickSection()
{
    // QByteArray containing the ISO formatted string representation of the
    // current date and time encoded using UTF-8.
    auto timeString = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();
    return writeStringSection(SectionType_Timetick, timeString);
}

bool ListFileWriter::writeEndSection()
{
    auto timeString = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();
    return writeStringSection(SectionType_End, timeString);
}

bool ListFileWriter::writePauseSection(ListfileSections::PauseAction pauseAction)
{
    const auto &lfc = listfile_constants();

    u32 sectionWords = 1;
    u32 header = (SectionType_Pause << lfc.SectionTypeShift) & lfc.SectionTypeMask;
    header |= (sectionWords << lfc.SectionSizeShift) & lfc.SectionSizeMask;

    u32 content = static_cast<u32>(pauseAction);

    if (m_out->write((const char *)&header, sizeof(header)) != sizeof(header))
        return false;

    if (m_out->write((const char *)&content, sizeof(content)) != sizeof(content))
        return false;

    return true;
}

std::pair<std::unique_ptr<VMEConfig>, std::error_code>
read_config_from_listfile(
    ListFile *listfile,
    std::function<void (const QString &msg)> logger)
{
    auto json = listfile->getVMEConfigJSON();

    //qDebug().noquote() << "read VMEConfig JSON from listfile:" << QJsonDocument(json).toJson(QJsonDocument::Indented);

    mesytec::mvme::vme_config::json_schema::SchemaUpdateOptions updateOptions;
    updateOptions.skip_v4_VMEScriptVariableUpdate = true;

    json = mesytec::mvme::vme_config::json_schema::convert_vmeconfig_to_current_version(json, logger, updateOptions);

    //qDebug().noquote() << "VMEConfig JSON after schema updates:" << QJsonDocument(json).toJson(QJsonDocument::Indented);

    auto vmeConfig = std::make_unique<VMEConfig>();
    auto ec = vmeConfig->read(json);

    return std::pair<std::unique_ptr<VMEConfig>, std::error_code>(
        std::move(vmeConfig), ec);
}
