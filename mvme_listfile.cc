#include "mvme_listfile.h"
#include "globals.h"
#include "mvme_config.h"
#include <QJsonObject>
#include <QDebug>

using namespace listfile;

void dump_event_buffer(QTextStream &out, const DataBuffer *eventBuffer)
{
    QString buf;
    BufferIterator iter(eventBuffer->data, eventBuffer->used, BufferIterator::Align32);

    while (iter.longwordsLeft())
    {
        u32 sectionHeader = iter.extractU32();
        int sectionType   = (sectionHeader & SectionTypeMask) >> SectionTypeShift;
        u32 sectionSize   = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

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
                    u32 eventType = (sectionHeader & EventTypeMask) >> EventTypeShift;
                    out << buf.sprintf("Event section: eventHeader=0x%08x, eventType=%d, eventSize=%u\n",
                                       sectionHeader, eventType, sectionSize);

                    u32 wordsLeft = sectionSize;

                    while (wordsLeft)
                    {
                        u32 subEventHeader = iter.extractU32();
                        --wordsLeft;
                        u32 moduleType = (subEventHeader & ModuleTypeMask) >> ModuleTypeShift;
                        u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;
                        QString moduleString = VMEModuleShortNames.value(static_cast<VMEModuleType>(moduleType), "unknown");

                        out << buf.sprintf("  subEventHeader=0x%08x, moduleType=%u (%s), subEventSize=%u\n",
                                           subEventHeader, moduleType, moduleString.toLatin1().constData(), subEventSize);

                        for (u32 i=0; i<subEventSize; ++i)
                        {
                            u32 subEventData = iter.extractU32();
                            //out << buf.sprintf("    %u = 0x%08x\n", i, subEventData);
                        }
                        wordsLeft -= subEventSize;
                    }
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

ListFile::ListFile(const QString &fileName)
{
    m_file.setFileName(fileName);
}

bool ListFile::open()
{
    return m_file.open(QIODevice::ReadOnly);
}

QJsonObject ListFile::getDAQConfig()
{
    if (m_configJson.isEmpty())
    {
        qint64 savedPos = m_file.pos();
        m_file.seek(0);

        QByteArray configData;

        while (true)
        {
            u32 sectionHeader = 0;

            if (m_file.read((char *)&sectionHeader, sizeof(sectionHeader)) != sizeof(sectionHeader))
                break;

            int sectionType  = (sectionHeader & SectionTypeMask) >> SectionTypeShift;
            u32 sectionWords = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

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
        m_configJson = QJsonDocument::fromJson(configData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError)
        {
            qDebug() << "Parse error: " << parseError.errorString();
        }
    }

    if (!m_configJson.isEmpty())
    {
        qDebug() << "listfile config json:" << m_configJson.toJson();
        return m_configJson.object();
    }

    return QJsonObject();
}

bool ListFile::seek(qint64 pos)
{
    qDebug() << &m_file << m_file.fileName() << m_file.isOpen();
    return m_file.seek(pos);
}

bool ListFile::readNextSection(DataBuffer *buffer)
{
    buffer->reserve(sizeof(u32));
    buffer->used = 0;

    if (m_file.read((char *)buffer->data, sizeof(u32)) != sizeof(u32))
        return false;

    buffer->used = sizeof(u32);

    u32 sectionHeader = *((u32 *)buffer->data);
    u32 sectionWords = (sectionHeader & SectionSizeMask) >> SectionSizeShift;
    qint64 sectionSize = sectionWords * sizeof(u32);

    if (sectionSize == 0)
        return true;

    qDebug() << sectionSize;

    buffer->reserve(sectionSize + sizeof(u32));

    if (m_file.read((char *)(buffer->data + buffer->used), sectionSize) != sectionSize)
        return false;

    buffer->used += sectionSize;
    return true;
}

s32 ListFile::readSectionsIntoBuffer(DataBuffer *buffer)
{
    // FIXME: sloooooow! Maybe completely fill the buffer, then search until
    // the last complete event is found and set buffer used to that offset.

    s32 sectionsRead = 0;

    while (!m_file.atEnd() && buffer->free() >= sizeof(u32))
    {
        u32 sectionHeader = 0;

        qint64 savedPos = m_file.pos();

        if (m_file.read((char *)&sectionHeader, sizeof(u32)) != sizeof(u32))
            return -1;

        m_file.seek(savedPos);

        u32 sectionWords = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

        qint64 bytesToRead = sectionWords * sizeof(u32) + sizeof(u32);

        if ((qint64)buffer->free() < bytesToRead)
            break;

        if (m_file.read((char *)(buffer->data + buffer->used), bytesToRead) != bytesToRead)
            return -1;

        buffer->used += bytesToRead;
        ++sectionsRead;
    }
    return sectionsRead;
}

static const size_t ListFileBufferSize = 1 * 1024 * 1024;

ListFileWorker::ListFileWorker(QObject *parent)
    : QObject(parent)
    , m_buffer(new DataBuffer(ListFileBufferSize))
{
}

ListFileWorker::~ListFileWorker()
{
    delete m_buffer;
}

void ListFileWorker::setListFile(ListFile *listFile)
{
    m_listFile = listFile;
}

void ListFileWorker::readNextBuffer()
{
    if (!m_listFile) return;

    m_buffer->used = 0;

    s32 sectionsRead = m_listFile->readSectionsIntoBuffer(m_buffer);

    qDebug() << __PRETTY_FUNCTION__ << sectionsRead << m_buffer->used;

    if (sectionsRead > 0)
    {
        emit mvmeEventBufferReady(m_buffer);
    }
    else
    {
        emit endOfFileReached();
    }
}
