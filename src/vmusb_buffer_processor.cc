#include "vmusb_buffer_processor.h"
#include "mvme_context.h"
#include "vmusb.h"
#include "mvme_listfile.h"
#include <memory>
#include <QCoreApplication>
#include <QDateTime>
#include <QtMath>
#include <QJsonObject>

using namespace vmusb_constants;
using namespace listfile;

#if 0
void format_vmusb_eventbuffer(DataBuffer *buffer, QTextStream &out)
{
    QString tmp;
    BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align16);

    u32 eventHeader = iter.extractShortword();

    u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
    bool partialEvent   = eventHeader & Buffer::ContinuationMask;
    u32 eventLength     = eventHeader & Buffer::EventLengthMask;

    out << "Buffer: bytes=" << buffer->used
        << ", shortwords=" << buffer->used/sizeof(u16)
        << ", longwords=" << buffer->used/sizeof(u32)
        << endl;

    out << "StackID=" << stackID << ", partial=" << partialEvent << ", eventLength=" << eventLength << endl;

    while (iter.longwordsLeft())
    {
        tmp.sprintf("0x%08x", iter.extractLongword());
        out << tmp << endl;
    }
    while (iter.shortwordsLeft())
    {
        tmp.sprintf("0x%04x", iter.extractShortword());
        out << tmp << endl;
    }
    while (iter.bytesLeft())
    {
        tmp.sprintf("0x%02x", iter.extractByte());
        out << tmp << endl;
    }
}

struct DataProcessorPrivate
{
    MVMEContext *context;
    QTime time;
    size_t buffersInInterval = 0;
    QMap<int, size_t> buffersPerStack;
};

DataProcessor::DataProcessor(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_d(new DataProcessorPrivate)
{
    m_d->context = context;
    m_d->time.start();
}

/* Process one event buffer containing one or more subevents. */
void DataProcessor::processBuffer(DataBuffer *buffer)
{
    static size_t totalBuffers = 0;
    ++totalBuffers;
    ++m_d->buffersInInterval;

    if (m_d->time.elapsed() > 1000)
    {
        auto buffersPerSecond = (float)m_d->buffersInInterval / (m_d->time.elapsed() / 1000.0);
        qDebug() << "buffers processed =" << totalBuffers
            << "buffers/sec =" << buffersPerSecond;

        m_d->time.restart();
        m_d->buffersInInterval = 0;

        QString buf;
        QTextStream stream(&buf);
        format_vmusb_eventbuffer(buffer, stream);
        emit eventFormatted(buf);
    }

    emit bufferProcessed(buffer);
}
#endif

static void processQtEvents(QEventLoop::ProcessEventsFlags flags = QEventLoop::AllEvents)
{
    QCoreApplication::processEvents(flags);
}

VMUSBBufferProcessor::VMUSBBufferProcessor(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
    , m_localEventBuffer(27 * 1024 * 2)
    , m_listFileWriter(new ListFileWriter(this))
{}

void VMUSBBufferProcessor::beginRun()
{
    resetRunState();

    QString outPath = m_context->getConfig()->getListFileOutputDirectory();
    bool listFileOutputEnabled = m_context->getConfig()->isListFileOutputEnabled();

    if (listFileOutputEnabled && outPath.size())
    {
        auto now = QDateTime::currentDateTime();
        QString outFilename = outPath + '/' + now.toString("yyyyMMdd_HHmmss") + ".mvmelst";
        m_listFileOut.setFileName(outFilename);

        emit logMessage(QString("Writing to listfile %1").arg(outFilename));

        if (m_listFileOut.exists())
        {
            throw QString("Error: listFile %1 exists");
        }

        if (!m_listFileOut.open(QIODevice::WriteOnly))
        {
            throw QString("Error opening listFile %1 for writing: %2")
                .arg(m_listFileOut.fileName())
                .arg(m_listFileOut.errorString())
                ;
        }

        m_listFileWriter->setOutputDevice(&m_listFileOut);

        QJsonObject configObject;
        m_context->write(configObject);
        QJsonDocument doc(configObject);

        if (!m_listFileWriter->writeConfig(doc.toJson()))
        {
            throw QString("Error writing to %1: %2")
                .arg(m_listFileOut.fileName())
                .arg(m_listFileOut.errorString());
        }

        getStats()->listFileBytesWritten = m_listFileWriter->bytesWritten();
    }
}

void VMUSBBufferProcessor::endRun()
{
    if (m_listFileOut.isOpen())
    {
        if (!m_listFileWriter->writeEndSection())
        {
            throw QString("Error writing to %1: %2")
                .arg(m_listFileOut.fileName())
                .arg(m_listFileOut.errorString());
        }

        getStats()->listFileBytesWritten = m_listFileWriter->bytesWritten();

        m_listFileOut.close();
    }

    auto queue = m_context->getFreeBuffers();
    getStats()->freeBuffers = queue->size();
}

void VMUSBBufferProcessor::resetRunState()
{
    auto eventConfigs = m_context->getEventConfigs();
    m_eventConfigByStackID.clear();

    for (auto config: eventConfigs)
    {
        m_eventConfigByStackID[config->stackID] = config;
    }
}

bool VMUSBBufferProcessor::processBuffer(DataBuffer *readBuffer)
{
    auto stats = getStats();
    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());

    BufferIterator iter(readBuffer->data, readBuffer->used, BufferIterator::Align16);

    DataBuffer *outputBuffer = getFreeBuffer();

    if (!outputBuffer)
    {
        outputBuffer = &m_localEventBuffer;
    }

    // XXX: Just use double the size of the read buffer for now. This way all additional data will fit.
    outputBuffer->reserve(readBuffer->used * 2);
    outputBuffer->used = 0;

    try
    {
        u32 header1 = iter.extractWord();

        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;

        const double alpha = 0.1;
        stats->vmusbAvgEventsPerBuffer = (alpha * numberOfEvents) + (1.0 - alpha) * stats->vmusbAvgEventsPerBuffer;

        if (lastBuffer || scalerBuffer || continuousMode || multiBuffer)
        {
            qDebug("buffer_size=%u, header1: 0x%08x, lastBuffer=%d, scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
                   readBuffer->used, header1, lastBuffer, scalerBuffer, continuousMode, multiBuffer, numberOfEvents);
        }

        if (vmusb->getMode() & GlobalMode::HeaderOptMask)
        {
            u32 header2 = iter.extractWord();
            u16 numberOfWords = header2 & Buffer::NumberOfWordsMask;
            //qDebug("header2: numberOfWords=%u, bytes in readBuffer=%u", numberOfWords, readBuffer->used);
        }

        bool skipBuffer = false;

        for (u16 eventIndex=0; eventIndex < numberOfEvents; ++eventIndex)
        {
            try 
            {
                if (!processEvent(iter, outputBuffer))
                {
                    emit logMessage(QString(QSL("VMUSB Error: processEvent() returned false, skipping buffer, eventIndex=%1, numberOfEvents=%2, header=0x%3"))
                                    .arg(eventIndex)
                                    .arg(numberOfEvents)
                                    .arg(header1, 8, 16, QLatin1Char('0'))
                                   );
                    skipBuffer = true;
                    break;
                }
            }
            catch (const end_of_buffer &)
            {
                emit logMessage(QString("VMUSB Error: end_of_buffer from processEvent(): eventIndex=%1, numberOfEvents=%2, header=0x%3")
                                .arg(eventIndex)
                                .arg(numberOfEvents)
                                .arg(header1, 8, 16, QLatin1Char('0'))
                               );
                throw;
            }
        }

        if (!skipBuffer)
        {
            if (iter.shortwordsLeft() >= 2)
            {
                for (int i=0; i<2; ++i)
                {
                    u16 bufferTerminator = iter.extractU16();
                    if (bufferTerminator != Buffer::BufferTerminator)
                    {
                        emit logMessage(QString("VMUSB Error: unexpected buffer terminator 0x%1")
                                        .arg(bufferTerminator, 4, 16, QLatin1Char('0')));
                    }
                }
            }
            else
            {
                emit logMessage(QSL("VMUSB Error: no terminator words found"));
            }

            if (iter.bytesLeft() != 0)
            {
                emit logMessage(QString("VMUSB Error: %1 bytes left in buffer")
                                .arg(iter.bytesLeft()));

                while (iter.longwordsLeft())
                {
                    emit logMessage(QString(QSL("  0x%1"))
                                    .arg(iter.extractU32(), 8, 16, QLatin1Char('0')));
                }

                while (iter.wordsLeft())
                {
                    emit logMessage(QString(QSL("  0x%1"))
                                    .arg(iter.extractU16(), 4, 16, QLatin1Char('0')));
                }

                while (iter.bytesLeft())
                {
                    emit logMessage(QString(QSL("  0x%1"))
                                    .arg(iter.extractU8(), 2, 16, QLatin1Char('0')));
                }
            }

            if (m_listFileOut.isOpen())
            {
                if (!m_listFileWriter->writeBuffer(reinterpret_cast<const char *>(outputBuffer->data),
                                                   outputBuffer->used))
                {
                    throw QString("Error writing to listfile '%1': %2")
                        .arg(m_listFileOut.fileName())
                        .arg(m_listFileOut.errorString());
                }
                getStats()->listFileBytesWritten = m_listFileWriter->bytesWritten();
            }

            QTextStream out(stdout);
            dump_mvme_buffer(out, outputBuffer, false);

            if (outputBuffer != &m_localEventBuffer)
            {
                emit mvmeEventBufferReady(outputBuffer);
            }
            else
            {
                getStats()->droppedBuffers++;
            }

            return true;
        }
    }
    catch (const end_of_buffer &)
    {
        emit logMessage(QSL("VMUSB Error: end of readBuffer reached unexpectedly!"));
        getStats()->buffersWithErrors++;
    }

    if (outputBuffer != &m_localEventBuffer)
    {
        addFreeBuffer(outputBuffer);
    }

    return false;
}

/* Process one VMUSB event, transforming it into a MVME event.
 * MVME Event structure:
 * Event Header
 *   SubeventHeader (== Module header)
 *     Raw module contents
 *     EndMarker
 *   SubeventHeader (== Module header)
 *     Raw module contents
 *     EndMarker
 * EndMarker
 * Event Header
 * ...
 */
bool VMUSBBufferProcessor::processEvent(BufferIterator &iter, DataBuffer *outputBuffer)
{
    if (iter.shortwordsLeft() < 1)
    {
        emit logMessage(QString(QSL("VMUSB Error: processEvent(): end of buffer when extracting event header")));
        return false;
    }

    u16 eventHeader = iter.extractU16();

    u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
    bool partialEvent   = eventHeader & Buffer::ContinuationMask; // TODO: handle this!
    u32 eventLength     = eventHeader & Buffer::EventLengthMask; // in 16-bit words

    //qDebug("eventHeader=0x%08x, stackID=%u, partialEvent=%d, eventLength=%u short words",
    //           eventHeader, stackID, partialEvent, eventLength);

    if (partialEvent)
    {
        qDebug("eventHeader=0x%08x, stackID=%u, partialEvent=%d, eventLength=%u shorts",
               eventHeader, stackID, partialEvent, eventLength);
        qDebug() << "===== Error: partial event support not implemented! ======";
        emit logMessage(QSL("VMUSB Error: got a partial event"));
        iter.skip(sizeof(u16), eventLength);
        return false;
    }

    if (!m_eventConfigByStackID.contains(stackID))
    {
        //qDebug("===== Error: no event config for stackID=%u! ======", stackID);
        emit logMessage(QString(QSL("VMUSB: No event config for stackID=%1, skipping event")).arg(stackID));
        iter.skip(sizeof(u16), eventLength);
        return false;
    }

    BufferIterator eventIter(iter.buffp, eventLength * sizeof(u16), BufferIterator::Align16);

    //qDebug() << "eventIter size =" << eventIter.bytesLeft() << " bytes";

    auto eventConfig = m_eventConfigByStackID[stackID];
    int moduleIndex = 0;
    u32 *outp = (u32 *)(outputBuffer->data + outputBuffer->used);
    u32 *mvmeEventHeader = (u32 *)outp++;

    size_t eventSize = 0;

    /* Store the event type, which is just the index into the event config
     * array in the header. */
    int eventType = m_context->getEventConfigs().indexOf(eventConfig);

    *mvmeEventHeader = (SectionType_Event << SectionTypeShift) & SectionTypeMask;
    *mvmeEventHeader |= (eventType << EventTypeShift) & EventTypeMask;

    for (int moduleIndex=0; moduleIndex<eventConfig->modules.size(); ++moduleIndex)
    {
        size_t subEventSize = 0; // in 32 bit words
        auto module = eventConfig->modules[moduleIndex];

        u32 *moduleHeader = outp++;
        *moduleHeader = (((u32)module->type) << ModuleTypeShift) & ModuleTypeMask;

        // extract and copy data until we used up the whole event length
        // or until BerrMarker and EndMarker have been found
        while (eventIter.longwordsLeft() >= 2)
        {
            /* TODO: this assumes 32 bit data from the module and BerrMarker
             * followed by EndMarker. Support modules/readout stacks
             * yielding 16 bit data and don't require a BerrMarker.
             * Note: just pad 16-bit data with zeroes. */
            u32 data = eventIter.extractU32();
            if (data == BerrMarker && eventIter.peekU32() == EndMarker)
            {
                *outp++ = eventIter.extractU32(); // copy the EndMarker
                ++subEventSize;

                *moduleHeader |= (subEventSize << SubEventSizeShift) & SubEventSizeMask;
                eventSize += subEventSize + 1; // +1 for the moduleHeader
                break;
            }
            else
            {
                *outp++ = data;
                ++subEventSize;
            }
        }
    }

    iter.buffp = eventIter.buffp; // advance the buffer iterator

    if (eventIter.bytesLeft())
    {
        emit logMessage(QString(QSL("VMUSB Error: %1 bytes left in event"))
                        .arg(eventIter.bytesLeft()));


        while (eventIter.longwordsLeft())
        {
            emit logMessage(QString(QSL("  0x%1"))
                            .arg(eventIter.extractU32(), 8, 16, QLatin1Char('0')));
        }

        while (eventIter.wordsLeft())
        {
            emit logMessage(QString(QSL("  0x%1"))
                            .arg(eventIter.extractU16(), 4, 16, QLatin1Char('0')));
        }

        while (eventIter.bytesLeft())
        {
            emit logMessage(QString(QSL("  0x%1"))
                            .arg(eventIter.extractU8(), 2, 16, QLatin1Char('0')));
        }
    }

    // Add an EndMarker at the end of the event
    *outp++ = EndMarker;
    ++eventSize;

    *mvmeEventHeader |= (eventSize << SectionSizeShift) & SectionSizeMask;
    outputBuffer->used = (u8 *)outp - outputBuffer->data;

    return true;
}

void VMUSBBufferProcessor::addFreeBuffer(DataBuffer *buffer)
{
    auto queue = m_context->getFreeBuffers();
    queue->enqueue(buffer);
    getStats()->freeBuffers = queue->size();
}

DataBuffer* VMUSBBufferProcessor::getFreeBuffer()
{
    auto queue = m_context->getFreeBuffers();
    getStats()->freeBuffers = queue->size();

    // Process pending events, then check if buffers are available

    if (!queue->size())
    {
        processQtEvents(/*QEventLoop::WaitForMoreEvents*/);
    }

    if (queue->size())
    {
        return queue->dequeue();
    }

    return 0;
}

DAQStats *VMUSBBufferProcessor::getStats()
{
    return &m_context->getDAQStats();
}
