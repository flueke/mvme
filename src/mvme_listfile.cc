/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "mvme_listfile.h"
#include "globals.h"
#include "vme_config.h"
#include "util/perf.h"
#include "util_zip.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QtMath>
#include <QElapsedTimer>
#include <QThread>

#include <quazip.h>
#include <quazipfile.h>

#include "threading.h"

#define LISTFILE_VERBOSE 0

const int listfile_v0::Version;
const int listfile_v0::FirstSectionOffset;
const int listfile_v0::SectionMaxWords;
const int listfile_v0::SectionMaxSize;
const int listfile_v0::SectionTypeMask;
const int listfile_v0::SectionTypeShift;
const int listfile_v0::SectionSizeMask;
const int listfile_v0::SectionSizeShift;
const int listfile_v0::EventTypeMask;
const int listfile_v0::EventTypeShift;
const int listfile_v0::ModuleTypeMask;
const int listfile_v0::ModuleTypeShift;
const int listfile_v0::SubEventMaxWords;
const int listfile_v0::SubEventMaxSize;
const int listfile_v0::SubEventSizeMask;
const int listfile_v0::SubEventSizeShift;

const int listfile_v1::Version;
const int listfile_v1::FirstSectionOffset;
const int listfile_v1::SectionMaxWords;
const int listfile_v1::SectionMaxSize;
const int listfile_v1::SectionTypeMask;
const int listfile_v1::SectionTypeShift;
const int listfile_v1::SectionSizeMask;
const int listfile_v1::SectionSizeShift;
const int listfile_v1::EventTypeMask;
const int listfile_v1::EventTypeShift;
const int listfile_v1::ModuleTypeMask;
const int listfile_v1::ModuleTypeShift;
const int listfile_v1::SubEventMaxWords;
const int listfile_v1::SubEventMaxSize;
const int listfile_v1::SubEventSizeMask;
const int listfile_v1::SubEventSizeShift;

using namespace ListfileSections;

template<typename LF>
void dump_mvme_buffer(QTextStream &out, const DataBuffer *eventBuffer, bool dumpData)
{
    QString buf;
    BufferIterator iter(eventBuffer->data, eventBuffer->used, BufferIterator::Align32);

    while (iter.longwordsLeft())
    {
        u32 sectionHeader = iter.extractU32();
        int sectionType   = (sectionHeader & LF::SectionTypeMask) >> LF::SectionTypeShift;
        u32 sectionSize   = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;

        out << "eventBuffer: " << eventBuffer << ", used=" << eventBuffer->used
            << ", size=" << eventBuffer->size
            << endl;
        out << buf.sprintf("sectionHeader=0x%08x, sectionType=%d, sectionSize=%u",
                           sectionHeader, sectionType, sectionSize)
            << endl;

        switch (sectionType)
        {
            case SectionType_Config:
                {
                    out << "Config section of size " << sectionSize << endl;
                    iter.skip(sectionSize * sizeof(u32));
                } break;

            case SectionType_Event:
                {
                    u32 eventType = (sectionHeader & LF::EventTypeMask) >> LF::EventTypeShift;
                    out << buf.sprintf("Event section: eventHeader=0x%08x, eventType=%d, eventSize=%u",
                                       sectionHeader, eventType, sectionSize)
                        << endl;

                    u32 wordsLeft = sectionSize;

                    while (wordsLeft > 1)
                    {
                        u32 subEventHeader = iter.extractU32();
                        --wordsLeft;
                        u32 moduleType = (subEventHeader & LF::ModuleTypeMask) >> LF::ModuleTypeShift;
                        u32 subEventSize = (subEventHeader & LF::SubEventSizeMask) >> LF::SubEventSizeShift;

                        out << buf.sprintf("  subEventHeader=0x%08x, moduleType=%u, subEventSize=%u",
                                           subEventHeader, moduleType, subEventSize)
                            << endl;

                        for (u32 i=0; i<subEventSize; ++i)
                        {
                            u32 subEventData = iter.extractU32();
                            if (dumpData)
                                out << buf.sprintf("    %u = 0x%08x", i, subEventData) << endl;
                        }
                        wordsLeft -= subEventSize;
                    }

                    u32 eventEndMarker = iter.extractU32();
                    out << buf.sprintf("   eventEndMarker=0x%08x", eventEndMarker) << endl;
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

void dump_mvme_buffer_v0(QTextStream &out, const DataBuffer *eventBuffer, bool dumpData)
{
    dump_mvme_buffer<listfile_v0>(out, eventBuffer, dumpData);
}

void dump_mvme_buffer(QTextStream &out, const DataBuffer *eventBuffer, bool dumpData)
{
    dump_mvme_buffer<listfile_v1>(out, eventBuffer, dumpData);
}

ListFile::ListFile(const QString &fileName)
    : m_input(new QFile(fileName))
{
}

ListFile::ListFile(QuaZipFile *inFile)
    : m_input(inFile)
{
}

ListFile::~ListFile()
{
    delete m_input;
}

bool ListFile::open()
{
    // For ZIP file input it is assumed that the file has been opened before
    // being passed to our constructor. QFiles are opened here.

    if (auto inFile = qobject_cast<QFile *>(m_input))
    {
        if (!inFile->open(QIODevice::ReadOnly))
        {
            return false;
        }
    }


    /* Tries to read the FourCC ("MVME") and the 4-byte version number. The
     * very first listfiles did not have this preamble so it's not an error if
     * the FourCC does not match.
     *
     * In both cases (version 0 or version > 0) a complete preamble is stored
     * in m_preambleBuffer. */

    m_fileVersion = 0;
    const char *toCompare = listfile_v1::FourCC;
    const size_t bytesToRead = 4;
    char fourCC[bytesToRead + 1] = {};

    qint64 bytesRead = m_input->read(reinterpret_cast<char *>(fourCC), bytesToRead);

    if (bytesRead == bytesToRead
        && std::strncmp(reinterpret_cast<char *>(fourCC), toCompare, bytesToRead) == 0)
    {
        u32 version;
        bytesRead = m_input->read(reinterpret_cast<char *>(&version), sizeof(version));

        if (bytesRead == sizeof(version))
        {
            m_fileVersion = version;
        }

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

/* Note: this is very inefficient for ZIP files and should be used sparingly
 * and only to look for a position near the start of the file. */
static bool seek_in_listfile(QIODevice *input, qint64 pos)
{
    if (auto inFile = qobject_cast<QFile *>(input))
    {
        return inFile->seek(pos);
    }
    else if (auto inZipFile = qobject_cast<QuaZipFile *>(input))
    {
        inZipFile->close();

        if (!inZipFile->open(QIODevice::ReadOnly))
        {
            return false;
        }

        while (pos > 0)
        {
            char c;
            inZipFile->read(&c, sizeof(c));
            --pos;
        }

        return true;
    }

    InvalidCodePath;

    return false;
}

template<typename LF>
QJsonObject get_daq_config(QIODevice &m_file)
{
    qint64 savedPos = m_file.pos();

    seek_in_listfile(&m_file, LF::FirstSectionOffset);

    QByteArray configData;

    while (true)
    {
        u32 sectionHeader = 0;

        if (m_file.read((char *)&sectionHeader, sizeof(sectionHeader)) != sizeof(sectionHeader))
            break;

        int sectionType  = (sectionHeader & LF::SectionTypeMask) >> LF::SectionTypeShift;
        u32 sectionWords = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;

        //qDebug() << "sectionType" << sectionType << ", sectionWords" << sectionWords;

        if (sectionType != SectionType_Config)
            break;

        if (sectionWords == 0)
            break;

        u32 sectionSize = sectionWords * sizeof(u32);

        QByteArray data = m_file.read(sectionSize);
        configData.append(data);
    }

    seek_in_listfile(&m_file, savedPos);

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

QJsonObject ListFile::getDAQConfig()
{
    if (m_configJson.isEmpty())
    {
        if (m_fileVersion == 0)
        {
            m_configJson = get_daq_config<listfile_v0>(*m_input);
        }
        else
        {
            m_configJson = get_daq_config<listfile_v1>(*m_input);
        }
    }

    return m_configJson;
}

bool ListFile::seekToFirstSection()
{
    auto offset = (m_fileVersion == 0) ? listfile_v0::FirstSectionOffset : listfile_v1::FirstSectionOffset;

    return seek(offset);
}

bool ListFile::seek(qint64 pos)
{
    qDebug() << m_input << getFileName() << m_input->isOpen();
    // Reset the currently saved sectionHeader on any seek.
    m_sectionHeaderBuffer = 0;
    return seek_in_listfile(m_input, pos);
}

template<typename LF>
bool read_next_section(QIODevice &m_file, DataBuffer *buffer, u32 *savedSectionHeader)
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

    u32 sectionWords = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;
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
    if (m_fileVersion == 0)
    {
        return read_next_section<listfile_v0>(*m_input, buffer, &m_sectionHeaderBuffer);
    }
    else
    {
        return read_next_section<listfile_v1>(*m_input, buffer, &m_sectionHeaderBuffer);
    }
}

template<typename LF>
s32 read_sections_into_buffer(QIODevice &m_file, DataBuffer *buffer, u32 *savedSectionHeader)
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

        u32 sectionWords = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;

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

        if (((sectionHeader & LF::SectionTypeMask) >> LF::SectionTypeShift) == ListfileSections::SectionType_End)
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
    if (m_fileVersion == 0)
    {
        return read_sections_into_buffer<listfile_v0>(*m_input, buffer, &m_sectionHeaderBuffer);
    }
    else
    {
        return read_sections_into_buffer<listfile_v1>(*m_input, buffer, &m_sectionHeaderBuffer);
    }
}

ListFileReader::ListFileReader(DAQStats &stats, QObject *parent)
    : QObject(parent)
    , m_stats(stats)
    , m_state(DAQState::Idle)
    , m_desiredState(DAQState::Idle)
{
}

ListFileReader::~ListFileReader()
{
}

void ListFileReader::setListFile(ListFile *listFile)
{
    m_listFile = listFile;
}

void ListFileReader::setEventsToRead(u32 eventsToRead)
{
    Q_ASSERT(m_state != DAQState::Running);
    m_eventsToRead = eventsToRead;
    m_logBuffers = (eventsToRead == 1);
}

void ListFileReader::start()
{
    Q_ASSERT(m_freeBuffers);
    Q_ASSERT(m_fullBuffers);
    Q_ASSERT(m_state == DAQState::Idle);

    if (m_state != DAQState::Idle || !m_listFile)
        return;

    m_listFile->seekToFirstSection();
    m_bytesRead = 0;
    m_totalBytes = m_listFile->size();
    m_stats.listFileTotalBytes = m_listFile->size();
    m_stats.start();

    mainLoop();
}

void ListFileReader::stop()
{
    qDebug() << __PRETTY_FUNCTION__ << "current state =" << DAQStateStrings[m_state];
    m_desiredState = DAQState::Stopping;
}

void ListFileReader::pause()
{
    qDebug() << __PRETTY_FUNCTION__ << "pausing";
    m_desiredState = DAQState::Paused;
}

void ListFileReader::resume()
{
    qDebug() << __PRETTY_FUNCTION__ << "resuming";
    m_desiredState = DAQState::Running;
}

static const u32 FreeBufferWaitTimeout_ms = 250;
static const double PauseSleep_ms = 250;

void ListFileReader::mainLoop()
{
    setState(DAQState::Running);

    logMessage(QString("Starting replay from %1").arg(m_listFile->getFileName()));

    while (true)
    {
        // pause
        if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            setState(DAQState::Paused);
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            setState(DAQState::Running);
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            break;
        }
        // stay in running state
        else if (m_state == DAQState::Running)
        {
            DataBuffer *buffer = nullptr;

            {
                QMutexLocker lock(&m_freeBuffers->mutex);
                while (m_freeBuffers->queue.isEmpty() && m_desiredState == DAQState::Running)
                {
                    m_freeBuffers->wc.wait(&m_freeBuffers->mutex, FreeBufferWaitTimeout_ms);
                }

                if (!m_freeBuffers->queue.isEmpty())
                {
                    buffer = m_freeBuffers->queue.dequeue();
                }
            }
            // The mutex is unlocked again at this point

            if (buffer)
            {
                buffer->used = 0;
                bool isBufferValid = false;

                if (unlikely(m_eventsToRead > 0))
                {
                    /* Read single events.
                     * Note: This is the unlikely case! This case only happens if
                     * the user pressed the "1 cycle / next event" button!
                     */

                    bool readMore = true;

                    // Skip non event sections
                    while (readMore)
                    {
                        isBufferValid = m_listFile->readNextSection(buffer);

                        if (isBufferValid)
                        {
                            m_stats.totalBuffersRead++;
                            m_stats.totalBytesRead += buffer->used;
                        }

                        if (isBufferValid && buffer->used >= sizeof(u32))
                        {
                            u32 sectionHeader = *reinterpret_cast<u32 *>(buffer->data);
                            u32 sectionType   = 0;

                            if (m_listFile->getFileVersion() == 0)
                            {
                                sectionType = (sectionHeader & listfile_v0::SectionTypeMask) >> listfile_v0::SectionTypeShift;
                            }
                            else
                            {
                                sectionType = (sectionHeader & listfile_v1::SectionTypeMask) >> listfile_v1::SectionTypeShift;
                            }

#if 0
                            u32 sectionWords  = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

                            qDebug() << __PRETTY_FUNCTION__ << "got section of type" << sectionType
                                << ", words =" << sectionWords
                                << ", size =" << sectionWords * sizeof(u32)
                                << ", buffer->used =" << buffer->used;
                            qDebug("%s sectionHeader=0x%08x", __PRETTY_FUNCTION__, sectionHeader);
#endif

                            if (sectionType == SectionType_Event)
                            {
                                readMore = false;
                            }
                        }
                        else
                        {
                            readMore = false;
                        }
                    }

                    if (--m_eventsToRead == 0)
                    {
                        // When done reading the requested amount of events transition
                        // to Paused state.
                        m_desiredState = DAQState::Paused;
                    }
                }
                else
                {
                    // Read until buffer is full
                    s32 sectionsRead = m_listFile->readSectionsIntoBuffer(buffer);
                    isBufferValid = (sectionsRead > 0);

                    if (isBufferValid)
                    {
                        m_stats.totalBuffersRead++;
                        m_stats.totalBytesRead += buffer->used;
                    }
                }

                if (!isBufferValid)
                {
                    // Reading did not succeed. Put the previously acquired buffer
                    // back into the free queue. No need to notfiy the wait
                    // condition as there's no one else waiting on it.
                    QMutexLocker lock(&m_freeBuffers->mutex);
                    m_freeBuffers->queue.enqueue(buffer);

                    setState(DAQState::Stopping);
                }
                else
                {
                    if (m_logBuffers && m_logger)
                    {
                        logMessage(">>> Begin buffer");
                        BufferIterator bufferIter(buffer->data, buffer->used, BufferIterator::Align32);
                        logBuffer(bufferIter, [this](const QString &str) { this->logMessage(str); });
                        logMessage("<<< End buffer");
                    }
                    // Push the valid buffer onto the output queue.
                    m_fullBuffers->mutex.lock();
                    m_fullBuffers->queue.enqueue(buffer);
                    m_fullBuffers->mutex.unlock();
                    m_fullBuffers->wc.wakeOne();
                }
            }
        }
        // paused
        else if (m_state == DAQState::Paused)
        {
            QThread::msleep(PauseSleep_ms);
        }
        else
        {
            Q_ASSERT(!"Unhandled case in ListFileReader::mainLoop()!");
        }
    }

    m_stats.stop();
    setState(DAQState::Idle);

    qDebug() << __PRETTY_FUNCTION__ << "exit";
}

void ListFileReader::setState(DAQState newState)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[newState];

    m_state = newState;
    m_desiredState = newState;
    emit stateChanged(newState);

    switch (newState)
    {
        case DAQState::Idle:
            emit replayStopped();
            break;

        case DAQState::Starting:
        case DAQState::Running:
        case DAQState::Stopping:
            break;

        case DAQState::Paused:
            emit replayPaused();
            break;
    }

    QCoreApplication::processEvents();
}

void ListFileReader::logMessage(const QString &str)
{
    if (m_logger)
        m_logger(str);
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

bool ListFileWriter::writePreamble()
{
    if (m_out->write(listfile_v1::FourCC, strlen(listfile_v1::FourCC)) != strlen(listfile_v1::FourCC))
        return false;

    u32 fileVersion = 1;
    if (m_out->write(reinterpret_cast<const char *>(&fileVersion), sizeof(fileVersion)) != sizeof(fileVersion))
        return false;

    return true;
}

bool ListFileWriter::writeConfig(QByteArray contents)
{
    using LF = listfile_v1;

    while (contents.size() % sizeof(u32))
    {
        contents.append(' ');
    }

    int configSize = contents.size();
    int configWords = configSize / sizeof(u32);
    int configSections = qCeil((float)configSize / (float)LF::SectionMaxSize);

    DataBuffer localBuffer(configSections * LF::SectionMaxSize // space for all config sections
                           + configSections * sizeof(u32));    // space for headers

    DataBuffer *buffer = &localBuffer;

    u8 *bufferP = buffer->data;
    const char *configP = contents.constData();

    while (configSections--)
    {
        u32 *sectionHeader = (u32 *)bufferP;
        bufferP += sizeof(u32);
        *sectionHeader = (SectionType_Config << LF::SectionTypeShift) & LF::SectionTypeMask;
        int sectionBytes = qMin(configSize, LF::SectionMaxSize);
        int sectionWords = sectionBytes / sizeof(u32);
        *sectionHeader |= (sectionWords << LF::SectionSizeShift) & LF::SectionSizeMask;

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
    dump_mvme_buffer(out, buffer, false);

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

bool ListFileWriter::writeEmptySection(SectionType sectionType)
{
    using LF = listfile_v1;

    u32 header = (sectionType << LF::SectionTypeShift) & LF::SectionTypeMask;

    if (m_out->write((const char *)&header, sizeof(header)) != sizeof(header))
        return false;

    m_bytesWritten += sizeof(header);

    return true;
}

bool ListFileWriter::writeEndSection()
{
    return writeEmptySection(SectionType_End);
}

bool ListFileWriter::writeTimetickSection()
{
    return writeEmptySection(SectionType_Timetick);
}

OpenListfileResult open_listfile(const QString &filename)
{
    OpenListfileResult result;

    if (filename.isEmpty())
        return result;

    // ZIP
    if (filename.toLower().endsWith(QSL(".zip")))
    {
        QString listfileFileName;

        // find and use the first .mvmelst file inside the archive
        {
            QuaZip archive(filename);

            if (!archive.open(QuaZip::mdUnzip))
            {
                throw make_zip_error("Could not open archive", &archive);
            }

            QStringList fileNames = archive.getFileNameList();

            auto it = std::find_if(fileNames.begin(), fileNames.end(), [](const QString &str) {
                return str.endsWith(QSL(".mvmelst"));
            });

            if (it == fileNames.end())
            {
                throw QString("No listfile found inside %1").arg(filename);
            }

            listfileFileName = *it;
        }

        Q_ASSERT(!listfileFileName.isEmpty());

        auto inFile = std::make_unique<QuaZipFile>(filename, listfileFileName);

        if (!inFile->open(QIODevice::ReadOnly))
        {
            throw make_zip_error("Could not open listfile", inFile.get());
        }

        result.listfile = std::make_unique<ListFile>(inFile.release());

        if (!result.listfile->open())
        {
            throw QString("Error opening listfile inside %1 for reading").arg(filename);
        }

        // try reading the VME config from inside the listfile
        auto json = result.listfile->getDAQConfig();

        if (json.isEmpty())
        {
            throw QString("Listfile does not contain a valid VME configuration");
        }

        /* Check if there's an analysis file inside the zip archive, read it,
         * store contents in state and decide on whether to directly load it.
         * */
        {
            QuaZipFile inFile(filename, QSL("analysis.analysis"));

            if (inFile.open(QIODevice::ReadOnly))
            {
                result.analysisBlob = inFile.readAll();
                result.analysisFilename = QSL("analysis.analysis");
            }
        }

        // Try to read the logfile from the archive
        {
            QuaZipFile inFile(filename, QSL("messages.log"));

            if (inFile.open(QIODevice::ReadOnly))
            {
                result.messages = inFile.readAll();
            }
        }
    }
    // Plain
    else
    {
        result.listfile = std::make_unique<ListFile>(filename);

        if (!result.listfile->open())
        {
            throw QString("Error opening %1 for reading").arg(filename);
        }

        auto json = result.listfile->getDAQConfig();

        if (json.isEmpty())
        {
            throw QString("Listfile does not contain a valid VME configuration");
        }
    }

    return result;
}
