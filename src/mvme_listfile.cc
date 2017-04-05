#include "mvme_listfile.h"
#include "globals.h"
#include "mvme_config.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QtMath>
#include <QElapsedTimer>

#include "threading.h"

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
        out << buf.sprintf("sectionHeader=0x%08x, sectionType=%d, sectionSize=%u\n",
                           sectionHeader, sectionType, sectionSize);

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
                    out << buf.sprintf("Event section: eventHeader=0x%08x, eventType=%d, eventSize=%u\n",
                                       sectionHeader, eventType, sectionSize);

                    u32 wordsLeft = sectionSize;

                    while (wordsLeft > 1)
                    {
                        u32 subEventHeader = iter.extractU32();
                        --wordsLeft;
                        u32 moduleType = (subEventHeader & LF::ModuleTypeMask) >> LF::ModuleTypeShift;
                        u32 subEventSize = (subEventHeader & LF::SubEventSizeMask) >> LF::SubEventSizeShift;
                        QString moduleString = VMEModuleShortNames.value(static_cast<VMEModuleType>(moduleType), "unknown");

                        out << buf.sprintf("  subEventHeader=0x%08x, moduleType=%u (%s), subEventSize=%u\n",
                                           subEventHeader, moduleType, moduleString.toLatin1().constData(), subEventSize);

                        for (u32 i=0; i<subEventSize; ++i)
                        {
                            u32 subEventData = iter.extractU32();
                            if (dumpData)
                                out << buf.sprintf("    %u = 0x%08x\n", i, subEventData);
                        }
                        wordsLeft -= subEventSize;
                    }

                    u32 eventEndMarker = iter.extractU32();
                    out << buf.sprintf("   eventEndMarker=0x%08x\n", eventEndMarker);
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
{
    m_file.setFileName(fileName);
}

bool ListFile::open()
{
    m_fileVersion = 0;
    bool result = m_file.open(QIODevice::ReadOnly);

    if (result)
    {
        const char *toCompare = listfile_v1::FourCC;
        size_t bytesToRead = strlen(toCompare);
        char fourCC[bytesToRead + 1] = {};

        qint64 bytesRead = m_file.read(reinterpret_cast<char *>(fourCC), bytesToRead);

        if (bytesRead == bytesToRead
            && std::strncmp(reinterpret_cast<char *>(fourCC), toCompare, bytesToRead) == 0)
        {
            u32 version;
            bytesRead = m_file.read(reinterpret_cast<char *>(&version), sizeof(version));

            if (bytesRead == sizeof(version))
            {
                m_fileVersion = version;
            }
        }

        seekToFirstSection();

        qDebug() << "detected listfile version" << m_fileVersion;
    }

    return result;
}

template<typename LF>
QJsonObject get_daq_config(QIODevice &m_file)
{
    qint64 savedPos = m_file.pos();
    m_file.seek(LF::FirstSectionOffset);

    QByteArray configData;

    while (true)
    {
        u32 sectionHeader = 0;

        if (m_file.read((char *)&sectionHeader, sizeof(sectionHeader)) != sizeof(sectionHeader))
            break;

        int sectionType  = (sectionHeader & LF::SectionTypeMask) >> LF::SectionTypeShift;
        u32 sectionWords = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;

        qDebug() << "sectionType" << sectionType << ", sectionWords" << sectionWords;

        if (sectionType != SectionType_Config)
            break;

        if (sectionWords == 0)
            break;

        u32 sectionSize = sectionWords * sizeof(u32);

        QByteArray data = m_file.read(sectionSize);
        configData.append(data);
    }

    m_file.seek(savedPos);

    QJsonParseError parseError; // TODO: make parse error message available to the user
    auto m_configJson = QJsonDocument::fromJson(configData, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qDebug() << "Parse error: " << parseError.errorString();
    }

    if (!m_configJson.isEmpty())
    {
        qDebug() << "listfile config json:" << m_configJson.toJson();
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
            m_configJson = get_daq_config<listfile_v0>(m_file);
        }
        else
        {
            m_configJson = get_daq_config<listfile_v1>(m_file);
        }
    }

    return m_configJson;
}

bool ListFile::seekToFirstSection()
{
    return seek((m_fileVersion == 0) ? listfile_v0::FirstSectionOffset : listfile_v1::FirstSectionOffset);
}

bool ListFile::seek(qint64 pos)
{
    qDebug() << &m_file << m_file.fileName() << m_file.isOpen();
    return m_file.seek(pos);
}

template<typename LF>
bool read_next_section(QIODevice &m_file, DataBuffer *buffer)
{
    buffer->reserve(sizeof(u32));
    buffer->used = 0;

    if (m_file.read((char *)buffer->data, sizeof(u32)) != sizeof(u32))
        return false;

    buffer->used = sizeof(u32);

    u32 sectionHeader = *((u32 *)buffer->data);
    u32 sectionWords = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;
    qint64 sectionSize = sectionWords * sizeof(u32);

    if (sectionSize == 0)
        return true;

    buffer->reserve(sectionSize + sizeof(u32));

    if (m_file.read((char *)(buffer->data + buffer->used), sectionSize) != sectionSize)
        return false;

    buffer->used += sectionSize;
    return true;
}

bool ListFile::readNextSection(DataBuffer *buffer)
{
    if (m_fileVersion == 0)
    {
        return read_next_section<listfile_v0>(m_file, buffer);
    }
    else
    {
        return read_next_section<listfile_v1>(m_file, buffer);
    }
}

template<typename LF>
s32 read_sections_into_buffer(QIODevice &m_file, DataBuffer *buffer)
{
    s32 sectionsRead = 0;

    while (!m_file.atEnd() && buffer->free() >= sizeof(u32))
    {
        u32 sectionHeader = 0;

        qint64 savedPos = m_file.pos();

        if (m_file.read((char *)&sectionHeader, sizeof(u32)) != sizeof(u32))
            return -1;

        u32 sectionWords = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;

        qint64 bytesToRead = sectionWords * sizeof(u32);

        // add the size of one u32 for the sectionHeader
        if ((qint64)buffer->free() < bytesToRead + (qint64)sizeof(u32))
        {
            // seek back to the sectionHeader
            m_file.seek(savedPos);
            break;
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
        return read_sections_into_buffer<listfile_v0>(m_file, buffer);
    }
    else
    {
        return read_sections_into_buffer<listfile_v1>(m_file, buffer);
    }
}

ListFileReader::ListFileReader(DAQStats &stats, QObject *parent)
    : QObject(parent)
    , m_stats(stats)
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
}

void ListFileReader::start()
{
    Q_ASSERT(m_freeBufferQueue);
    Q_ASSERT(m_filledBufferQueue);
    Q_ASSERT(m_state == DAQState::Idle);

    if (m_state != DAQState::Idle || !m_listFile)
        return;

    m_listFile->seekToFirstSection();
    m_bytesRead = 0;
    m_totalBytes = m_listFile->size();
    m_stats.listFileTotalBytes = m_listFile->size();
    m_stats.start();

    mainLoop();

    setState(DAQState::Idle);
    emit replayStopped();
}

void ListFileReader::stop()
{
    if (!(m_state == DAQState::Running || m_state == DAQState::Paused))
        return;

    m_desiredState = DAQState::Stopping;
}

void ListFileReader::pause()
{
    if (m_state == DAQState::Running)
        m_desiredState = DAQState::Paused;
}

void ListFileReader::resume()
{
    if (m_state == DAQState::Paused)
        m_desiredState = DAQState::Running;
}

static const u32 FreeBufferWaitTimeout_ms = 250;
static const u32 ProcessEventsMinInterval_ms = 500;

void ListFileReader::mainLoop()
{
    setState(DAQState::Running);

    QCoreApplication::processEvents();

    QElapsedTimer timeSinceLastProcessEvents;
    timeSinceLastProcessEvents.start();

    while (true)
    {
        // pause
        if (m_state == DAQState::Running && m_desiredState == DAQState::Paused)
        {
            setState(DAQState::Paused);
            // TODO: pause stats
        }
        // resume
        else if (m_state == DAQState::Paused && m_desiredState == DAQState::Running)
        {
            setState(DAQState::Running);
            // TODO: resume stats
        }
        // stop
        else if (m_desiredState == DAQState::Stopping)
        {
            m_stats.stop();
            break;
        }
        // stay in running state
        else if (m_state == DAQState::Running)
        {
            DataBuffer *buffer = nullptr;

            {
                QMutexLocker lock(&m_freeBufferQueue->mutex);
                while (m_freeBufferQueue->queue.isEmpty())
                {
                    m_freeBufferQueue->wc.wait(&m_freeBufferQueue->mutex, FreeBufferWaitTimeout_ms);
                }
                buffer = m_freeBufferQueue->queue.dequeue();
            }
            // The mutex is unlocked again at this point

            Q_ASSERT(buffer);

            buffer->used = 0;
            bool isBufferValid = false;

            if (m_eventsToRead > 0)
            {
                // Read single events
                bool readMore = true;

                // Skip non event sections
                while (readMore)
                {
                    isBufferValid = m_listFile->readNextSection(buffer);

                    if (isBufferValid)
                    {
                        m_stats.addBuffersRead(1);
                        m_stats.addBytesRead(buffer->used);
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
                    m_stats.addBuffersRead(1);
                    m_stats.addBytesRead(buffer->used);
                }
            }

            if (!isBufferValid)
            {
                // Reading did not succeed. Put the previously acquired buffer
                // back into the free queue. No need to notfiy the wait
                // condition as there's no one else waiting on it.
                QMutexLocker lock(&m_freeBufferQueue->mutex);
                m_freeBufferQueue->queue.enqueue(buffer);

                setState(DAQState::Stopping);
            }
            else
            {
                // Push the valid buffer onto the output queue.
                m_filledBufferQueue->mutex.lock();
                m_filledBufferQueue->queue.enqueue(buffer);
                m_filledBufferQueue->mutex.unlock();
                m_filledBufferQueue->wc.wakeOne();
            }
        }
        else if (m_state == DAQState::Paused)
        {
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
            timeSinceLastProcessEvents.restart();
        }
        else
        {
            Q_ASSERT(!"Unhandled case in ListFileReader::mainLoop()!");
        }

        // Process Qt events to be able to "receive" queued calls to our slots.
        if (timeSinceLastProcessEvents.elapsed() > ProcessEventsMinInterval_ms)
        {
            QCoreApplication::processEvents();
            timeSinceLastProcessEvents.restart();
        }
    }
}

void ListFileReader::setState(DAQState state)
{
    qDebug() << __PRETTY_FUNCTION__ << DAQStateStrings[m_state] << "->" << DAQStateStrings[state];
    m_state = state;
    m_desiredState = state;
    emit stateChanged(state);
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
    m_out = device;
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

bool ListFileWriter::writeEndSection()
{
    using LF = listfile_v1;

    u32 header = (SectionType_End << LF::SectionTypeShift) & LF::SectionTypeMask;

    if (m_out->write((const char *)&header, sizeof(header)) != sizeof(header))
        return false;

    m_bytesWritten += sizeof(header);

    return true;
}
