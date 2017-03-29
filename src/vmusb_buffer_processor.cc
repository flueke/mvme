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

//#define BPDEBUG

#if 0
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

#ifdef BPDEBUG
// Note: Assumption: VMUSBs HeaderOptMask option is not used!
void format_vmusb_buffer(DataBuffer *buffer, QTextStream &out)
{
    try
    {
        out << "Buffer: bytes=" << buffer->used
            << ", shortwords=" << buffer->used/sizeof(u16)
            << ", longwords=" << buffer->used/sizeof(u32)
            << endl;

        QString tmp;
        BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align16);


        u32 header1 = iter.extractWord();
        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;


        tmp = QString("header1=0x%1, numberOfEvents=%2, lastBuffer=%3, cont=%4, mult=%5")
            .arg(header1, 8, 16, QLatin1Char('0'))
            .arg(numberOfEvents)
            .arg(lastBuffer)
            .arg(continuousMode)
            .arg(multiBuffer)
            ;

        out << tmp << endl;

        for (u16 eventIndex = 0; eventIndex < numberOfEvents; ++eventIndex)
        {
            u32 eventHeader = iter.extractShortword();
            u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
            bool partialEvent   = eventHeader & Buffer::ContinuationMask;
            u32 eventLength     = eventHeader & Buffer::EventLengthMask;

            tmp = QString("event #%5, header=0x%1, stackID=%2, length=%3 shorts, partial=%4")
                .arg(eventHeader, 8, 16, QLatin1Char('0'))
                .arg(stackID)
                .arg(eventLength)
                .arg(partialEvent)
                .arg(eventIndex)
                ;

            out << tmp << endl;

            int col = 0;
            u32 longwordsLeft = eventLength / 2;

            while (longwordsLeft--)
            {
                tmp = QString("0x%1").arg(iter.extractU32(), 8, 16, QLatin1Char('0'));
                out << tmp;
                ++col;
                if (col < 8)
                {
                    out << " ";
                }
                else
                {
                    out << endl;
                    col = 0;
                }
            }

            u32 shortwordsLeft = eventLength - ((eventLength / 2) * 2);

            out << endl;
            col = 0;
            while (shortwordsLeft--)
            {
                tmp = QString("0x%1").arg(iter.extractU16(), 4, 16, QLatin1Char('0'));
                out << tmp;
                ++col;
                if (col < 8)
                {
                    out << " ";
                }
                else
                {
                    out << endl;
                    col = 0;
                }
            }
        }


        if (iter.bytesLeft())
        {
            out << endl;
            out << iter.bytesLeft() << " bytes left in buffer:" << endl;
            int col = 0;
            while (iter.bytesLeft())
            {
                tmp = QString("0x%1").arg(iter.extractU8(), 2, 16, QLatin1Char('0'));
                out << tmp;
                ++col;
                if (col < 8)
                {
                    out << " ";
                }
                else
                {
                    out << endl;
                    col = 0;
                }
            }
        }
    }
    catch (const end_of_buffer &)
    {
        out << "!!! end of buffer reached unexpectedly !!!" << endl;
    }
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
    Q_ASSERT(m_freeBufferQueue);
    Q_ASSERT(m_filledBufferQueue);

    m_vmusb = dynamic_cast<VMUSB *>(m_context->getController());

    if (!m_vmusb)
    {
        /* This should not happen but ensures that m_vmusb is set when
         * processBuffer() will be called later on. */
        throw QString(QSL("Error from VMUSBBufferProcessor: no VMUSB present!"));
    }

    resetRunState();

    QString outPath = m_context->getListFileDirectory();
    bool listFileOutputEnabled = m_context->isListFileOutputEnabled();

    // TODO: this needs to move into some generic listfile handler!
    if (listFileOutputEnabled && outPath.size())
    {
        auto now = QDateTime::currentDateTime();
        QString outFilename = outPath + '/' + now.toString("yyMMdd_HHmmss") + ".mvmelst";
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
        getStats()->listfileFilename = outFilename;

        QJsonObject daqConfigJson;
        m_context->getDAQConfig()->write(daqConfigJson);
        QJsonObject configJson;
        configJson["DAQConfig"] = daqConfigJson;
        QJsonDocument doc(configJson);

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
    Q_ASSERT(m_freeBufferQueue);
    Q_ASSERT(m_filledBufferQueue);

    auto stats = getStats();
    auto vmusb = m_vmusb;
    auto alignment = ((vmusb->getMode() & GlobalModeRegister::Align32Mask)
                      ? BufferIterator::Align32
                      : BufferIterator::Align16);

    u64 bufferNumber = stats->totalBuffersRead;

#ifdef BPDEBUG
    {
        QTextStream out(stdout);
        out << ">>>>> begin buffer #" << bufferNumber << endl;
        format_vmusb_buffer(readBuffer, out);
        out << "<<<<< end buffer #" << bufferNumber << endl;
    }
#endif


    BufferIterator iter(readBuffer->data, readBuffer->used, alignment);

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

#ifdef BPDEBUG
        emit logMessage(QString("buffer #%1, header1=0x%2, numberOfEvents=%3, lastBuffer=%4, cont=%5, mult=%6")
                        .arg(bufferNumber)
                        .arg(header1, 8, 16, QLatin1Char('0'))
                        .arg(numberOfEvents)
                        .arg(lastBuffer)
                        .arg(continuousMode)
                        .arg(multiBuffer)
                        );
#endif


#if 1
        if (lastBuffer || scalerBuffer || continuousMode || multiBuffer)
        {
            qDebug("buffer #%llu, buffer_size=%u, header1: 0x%08x, lastBuffer=%d"
                   ", scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
                   bufferNumber, readBuffer->used, header1, lastBuffer, scalerBuffer,
                   continuousMode, multiBuffer, numberOfEvents);
        }
#endif

        if (vmusb->getMode() & vmusb_constants::GlobalMode::HeaderOptMask)
        {
            u32 header2 = iter.extractWord();
            u16 numberOfWords = header2 & Buffer::NumberOfWordsMask;
            //qDebug("header2: numberOfWords=%u, bytes in readBuffer=%u", numberOfWords, readBuffer->used);
#ifdef BPDEBUG
            emit logMessage(QString("buffer #%1: extracted header2=0x%2, numberOfWords=%3")
                            .arg(bufferNumber)
                            .arg(header2, 8, 16, QLatin1Char('0'))
                            .arg(numberOfWords)
                           );
#endif
        }

        bool skipBuffer = false;

        for (u16 eventIndex=0; eventIndex < numberOfEvents; ++eventIndex)
        {
            try 
            {
                if (!processEvent(iter, outputBuffer, bufferNumber, eventIndex))
                {
                    emit logMessage(QString(QSL("VMUSB Error: (buffer #%4) processEvent() returned false, skipping buffer, eventIndex=%1, numberOfEvents=%2, header=0x%3"))
                                    .arg(eventIndex)
                                    .arg(numberOfEvents)
                                    .arg(header1, 8, 16, QLatin1Char('0'))
                                    .arg(bufferNumber)
                                   );
                    skipBuffer = true;
                    break;
                }
#if 0
                else
                {
                    emit logMessage(QString("(buffer #%2) good event for eventindex=%1")
                                    .arg(eventIndex)
                                    .arg(bufferNumber));
                }
#endif
            }
            catch (const end_of_buffer &)
            {
                emit logMessage(QString("VMUSB Error: (buffer #%4) end_of_buffer from processEvent(): eventIndex=%1, numberOfEvents=%2, header=0x%3")
                                .arg(eventIndex)
                                .arg(numberOfEvents)
                                .arg(header1, 8, 16, QLatin1Char('0'))
                                .arg(bufferNumber));
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
                        emit logMessage(QString("VMUSB Warning: (buffer #%2) unexpected buffer terminator 0x%1")
                                        .arg(bufferTerminator, 4, 16, QLatin1Char('0'))
                                        .arg(bufferNumber));
                    }
                }
            }
            else
            {
                emit logMessage(QSL("VMUSB Warning: (buffer #%1) no terminator words found at end of buffer")
                                .arg(bufferNumber));
            }

            if (iter.bytesLeft() != 0)
            {
                emit logMessage(QString("VMUSB Warning: (buffer #%3) %1 bytes left in buffer, numberOfEvents=%2")
                                .arg(iter.bytesLeft())
                                .arg(numberOfEvents)
                                .arg(bufferNumber)
                                );

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

            //QTextStream out(stdout);
            //dump_mvme_buffer(out, outputBuffer, false);

            if (outputBuffer != &m_localEventBuffer)
            {
                // It's not the local buffer -> put it into the queue of filled buffers
                m_filledBufferQueue->mutex.lock();
                m_filledBufferQueue->queue.enqueue(outputBuffer);
                m_filledBufferQueue->mutex.unlock();
                m_filledBufferQueue->wc.wakeOne();
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
        emit logMessage(QSL("VMUSB Warning: (buffer #%1) end of readBuffer reached unexpectedly!")
                        .arg(bufferNumber));
        getStats()->buffersWithErrors++;
    }

    if (outputBuffer != &m_localEventBuffer)
    {
        // Put the buffer back onto the free queue
        QMutexLocker lock(&m_freeBufferQueue->mutex);
        m_freeBufferQueue->queue.enqueue(outputBuffer);
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
bool VMUSBBufferProcessor::processEvent(BufferIterator &iter, DataBuffer *outputBuffer, u64 bufferNumber, u16 eventIndex)
{
    /* Returning false from this method will make processEvent() skip the
     * entire buffer. To skip only a single event do the skip in here and
     * return true. */



    if (iter.wordsLeft() < 1)
    {
        emit logMessage(QString(QSL("VMUSB Error: (buffer #%1) processEvent(): end of buffer when extracting event header"))
                        .arg(bufferNumber));
        return false;
    }

    u32 eventHeader = iter.extractWord();

    u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
    bool partialEvent   = eventHeader & Buffer::ContinuationMask;
    u32 eventLength     = eventHeader & Buffer::EventLengthMask; // in 16-bit words

#ifdef BPDEBUG
    emit logMessage(QString("buffer #%1, eventIndex=%2, eventHeader=0x%3, stackID=%4, eventLength=%5, partialEvent=%6")
                    .arg(bufferNumber)
                    .arg(eventIndex)
                    .arg(eventHeader, 8, 16, QLatin1Char('0'))
                    .arg(stackID)
                    .arg(eventLength)
                    .arg(partialEvent)
                   );
#endif

    if (iter.shortwordsLeft() < eventLength)
    {
        emit logMessage(QSL("VMUSB Error: (buffer #%1) event length exceeds buffer length, skipping buffer")
                        .arg(bufferNumber));
        return false;
    }

    if (stackID > StackIDMax)
    {
        emit logMessage(QString(QSL("VMUSB: (buffer #%2) Parsed stackID=%1 is out of range, skipping event"))
                        .arg(stackID)
                        .arg(bufferNumber));
        iter.skip(sizeof(u16), eventLength);
        return true;
    }

    if (!m_eventConfigByStackID.contains(stackID))
    {
        emit logMessage(QString(QSL("VMUSB: (buffer #%3) No event config for stackID=%1, eventLength=%2, skipping event"))
                        .arg(stackID)
                        .arg(eventLength)
                        .arg(bufferNumber));
        iter.skip(sizeof(u16), eventLength);
        return true;
    }

    if (partialEvent)
    {
        qDebug("eventHeader=0x%08x, stackID=%u, partialEvent=%d, eventLength=%u shorts",
               eventHeader, stackID, partialEvent, eventLength);
        qDebug() << "===== Error: partial event support not implemented! ======";
        emit logMessage(QString(QSL("VMUSB Error: (buffer #%1) got a partial event (not supported yet!)"))
                        .arg(bufferNumber));
        iter.skip(sizeof(u16), eventLength);
        return true;
    }

    /* Create a local iterator limited by the event length. A check above made
     * sure that the event length does not exceed the inputs size. */
    BufferIterator eventIter(iter.buffp, eventLength * sizeof(u16), iter.alignment);

    if (m_logBuffers)
    {
        logMessage(QString(">>> Begin event %1 in buffer #%2")
                   .arg(eventIndex)
                   .arg(bufferNumber));

        logBuffer(eventIter, [this](const QString &str) { logMessage(str); });

        logMessage(QString("<<< End event %1 in buffer #%2")
                   .arg(eventIndex)
                   .arg(bufferNumber));
    }

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

        /* Extract and copy data until we used up the whole event length or
         * until the EndMarker has been found.
         * VMUSB only knows about 16-bit marker words. When using 16-bit
         * alignment and two 16-bit markers it looks like a single 32-bit
         * marker word and everything works out.
         */
        while (eventIter.wordsLeft() >= 1)
        {
            // Note: this assumes 32 bit data alignment from the module!

            u32 data = eventIter.extractU32();

            if (data == EndMarker)
            {
#ifdef BPDEBUG
                emit logMessage(QString("buffer #%1, eventIndex=%2, found EndMarker!")
                                .arg(bufferNumber)
                                .arg(eventIndex)
                               );
#endif
                /* Add the marker to the output stream. */
                *outp++ = EndMarker;
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
                            .arg(eventIter.extractU16(),
                                 (eventIter.alignment == BufferIterator::Align16) ? 4 : 8,
                                 16, QLatin1Char('0')));
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

    iter.buffp = eventIter.buffp; // advance the buffer iterator

    return true;
}

DataBuffer* VMUSBBufferProcessor::getFreeBuffer()
{
    DataBuffer *result = nullptr;

    QMutexLocker lock(&m_freeBufferQueue->mutex);
    if (!m_freeBufferQueue->queue.isEmpty())
        result = m_freeBufferQueue->queue.dequeue();

    return result;
}

DAQStats *VMUSBBufferProcessor::getStats()
{
    return &m_context->getDAQStats();
}
