#include "vmusb_buffer_processor.h"
#include "mvme_context.h"
#include "vmusb.h"
#include "mvme_listfile.h"
#include <memory>
#include <QCoreApplication>
#include <QDateTime>

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
{}

void VMUSBBufferProcessor::beginRun()
{
    resetRunState();

    QString outPath = m_context->getConfig()->listFileOutputDirectory;

    if (m_listFileOutputEnabled && outPath.size())
    {
        auto now = QDateTime::currentDateTime();
        QString outFilename = outPath + '/' + now.toString(Qt::ISODate) + ".mvmelst";
        m_listFileOut.setFileName(outFilename);

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

        qDebug() << "writing listfile" << m_listFileOut.fileName();

        /* Write out a "config" section containing the complete run configuration. */

        auto configBytes = m_context->getConfig()->toJson();

        while (configBytes.size() % sizeof(u32))
        {
            configBytes.append('\0');
        }

        DataBuffer *buffer = getFreeBuffer();

        if (buffer)
        {
            buffer->reserve(configBytes.size() + sizeof(u32));
            u32 size = configBytes.size() / sizeof(u32);

            u32 *sectionHeader = (u32 *)buffer->data;
            *sectionHeader = (SectionType_Config << SectionTypeShift) & SectionTypeMask;
            *sectionHeader |= (size << SectionSizeShift) & SectionSizeMask;
            memcpy(buffer->data + sizeof(u32), configBytes.constData(), configBytes.size());
            buffer->used = configBytes.size() + sizeof(u32);

            if (m_listFileOut.isOpen())
            {
                m_listFileOut.write((const char *)buffer->data, buffer->used);
            }

            QTextStream out(stdout);
            dump_event_buffer(out, buffer);
            emit mvmeEventBufferReady(buffer);
        }
    }
}

void VMUSBBufferProcessor::endRun()
{
    // TODO: maybe write a "end run" event?
    if (m_listFileOut.isOpen())
    {
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
    auto vmusb = dynamic_cast<VMUSB *>(m_context->getController());

    BufferIterator iter(readBuffer->data, readBuffer->used, BufferIterator::Align16);

#if 1
    try
    {
#endif
        u32 header1 = iter.extractWord();

        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;

        if (true || lastBuffer || scalerBuffer || continuousMode || multiBuffer)
        {
            qDebug("buffer_size=%u, header1: 0x%08x, lastBuffer=%d, scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
                   readBuffer->used, header1, lastBuffer, scalerBuffer, continuousMode, multiBuffer, numberOfEvents);
        }

        if (vmusb->getMode() & GlobalMode::HeaderOptMask)
        {
            u32 header2 = iter.extractWord();
            u16 numberOfWords = header2 & Buffer::NumberOfWordsMask;
            qDebug("header2: numberOfWords=%u, bytes in readBuffer=%u", numberOfWords, readBuffer->used);
        }

        for (u16 eventIndex=0; eventIndex < numberOfEvents; ++eventIndex)
        {
            processEvent(iter);
        }

        if (iter.shortwordsLeft() >= 2)
        {
            for (int i=0; i<2; ++i)
            {
                u16 bufferTerminator = iter.extractU16();
                if (bufferTerminator != Buffer::BufferTerminator)
                {
                    qDebug("processBuffer() warning: unexpected buffer terminator 0x%04x", bufferTerminator);
                }
            }
        }
        else
        {
            qDebug("processBuffer() warning: no terminator words found!");
        }

        if (iter.bytesLeft() != 0)
        {
            qDebug("processBuffer() warning: %u bytes left in buffer!", iter.bytesLeft());

            for (size_t i=0; i<iter.longwordsLeft(); ++i)
                qDebug("  0x%08x", iter.extractU32());

            for (size_t i=0; i<iter.wordsLeft(); ++i)
                qDebug("  0x%04x", iter.extractU16());

            for (size_t i=0; i<iter.bytesLeft(); ++i)
                qDebug("  0x%02x", iter.extractU8());
        }
        else
        {
            qDebug("processBuffer(): buffer successfully processed!");
        }

        return true;
#if 1
    }
    catch (const end_of_buffer &)
    {
        qDebug("Error: end of readBuffer reached unexpectedly!");
    }
#endif

    return false;
}

/* Process one VMUSB event, transforming it into a MVME event.
 * MVME Event structure:
 * Event Header
 *   Module header
 *     Raw module contents
 *   Module header
 *     Raw module contents
 * ...
 */
bool VMUSBBufferProcessor::processEvent(BufferIterator &iter)
{
    std::unique_ptr<DataBuffer> outputBuffer;

    u16 eventHeader = iter.extractU16();

    u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
    bool partialEvent   = eventHeader & Buffer::ContinuationMask; // TODO: handle this!
    u32 eventLength     = eventHeader & Buffer::EventLengthMask; // in 16-bit words

    if (partialEvent)
    {
        qDebug("eventHeader=0x%08x, stackID=%u, partialEvent=%d, eventLength=%u",
               eventHeader, stackID, partialEvent, eventLength);
        qDebug() << "===== Error: partial event support not implemented! ======";
        iter.skip(sizeof(u16), eventLength); 
        return false;
    }

    if (!m_eventConfigByStackID.contains(stackID))
    {
        qDebug("===== Error: no event config for stackID=%u! ======", stackID);
        iter.skip(sizeof(u16), eventLength); 
        return false;
    }

    outputBuffer.reset(getFreeBuffer());

    if (!outputBuffer)
    {
        qDebug("VMUSBBufferProcessor:: outputBuffer is null");
        iter.skip(sizeof(u16), eventLength); 
        return false;
    }
    outputBuffer->used = 0;

    BufferIterator eventIter(iter.buffp, eventLength * sizeof(u16), BufferIterator::Align16);

    auto eventConfig = m_eventConfigByStackID[stackID];
    int moduleIndex = 0;
    u32 *outp = (u32 *)outputBuffer->data;
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
        // or until BerrMarker and EndOfModuleMarker have been found
        while (eventIter.longwordsLeft() >= 2)
        {
            /* TODO: this assumes 32 bit data from the module and BerrMarker
             * followed by EndOfModuleMarker. Support modules/readout stacks
             * yielding 16 bit data and don't require a BerrMarker.
             * Note: just pad 16-bit data with zeroes. */
            u32 data = eventIter.extractU32();
            if (data == BerrMarker && eventIter.peekU32() == EndOfModuleMarker)
            {
                eventIter.extractU32(); // skip past EndOfModuleMarker
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
        qDebug("===== Warning: %u bytes left in eventIter", eventIter.bytesLeft());
    }

    *mvmeEventHeader |= (eventSize << SectionSizeShift) & SectionSizeMask;
    outputBuffer->used = (u8 *)outp - outputBuffer->data;

    if (m_listFileOut.isOpen())
    {
        m_listFileOut.write((const char *)outputBuffer->data, outputBuffer->used);
    }


    QTextStream out(stdout);
    dump_event_buffer(out, outputBuffer.get());
    emit mvmeEventBufferReady(outputBuffer.release());
    //addFreeBuffer(outputBuffer.release());

    return true;
}

void VMUSBBufferProcessor::addFreeBuffer(DataBuffer *buffer)
{
    m_context->getFreeBuffers()->enqueue(buffer);
    qDebug() << __PRETTY_FUNCTION__ << m_context->getFreeBuffers()->size() << buffer;
}

DataBuffer* VMUSBBufferProcessor::getFreeBuffer()
{
    auto queue = m_context->getFreeBuffers();

    while (!queue->size() &&
            (m_context->getDAQState() == DAQState::Running ||
             m_context->getDAQState() == DAQState::Stopping))
    {
        // Avoid fast spinning by blocking if no events are pending
        processQtEvents(QEventLoop::WaitForMoreEvents);
    }
    return queue->dequeue();
}
