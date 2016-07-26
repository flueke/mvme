#include "vmusb_buffer_processor.h"
#include "mvme_context.h"
#include "vmusb.h"
#include <memory>
#include <QCoreApplication>

using namespace VMUSBConstants;

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

    try
    {
        u32 header1 = iter.extractWord();

        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;

        if (true || lastBuffer || scalerBuffer || continuousMode || multiBuffer)
        {
            qDebug("header1: 0x%08x, lastBuffer=%d, scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
                    header1, lastBuffer, scalerBuffer, continuousMode, multiBuffer, numberOfEvents);
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

        if (iter.shortwordsLeft())
        {
            while (iter.shortwordsLeft())
            {
                u16 bufferTerminator = iter.extractU16();
                if (bufferTerminator != 0xffff)
                {
                    qDebug("processBuffer() warning: buffer terminator != 0xffff");
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
        }
        else
        {
            qDebug("processBuffer(): buffer successfully processed!");
        }

        return true;
    }
    // TODO: handle these errors somehow (terminating readout?)
    catch (const end_of_buffer &)
    {
        qDebug("Error: end of readBuffer reached unexpectedly!");
    }

    return false;
}

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
    outputBuffer->used = 0;

    auto eventConfig = m_eventConfigByStackID[stackID];
    int moduleIndex = 0;
    u32 *outp = (u32 *)outputBuffer->data;
    u32 *mvmeEventHeader = (u32 *)outp++;

    size_t eventSize = 0;
    *mvmeEventHeader = m_context->getEventConfigs().indexOf(eventConfig); // store the event config index in the module header

    for (int moduleIndex=0; moduleIndex<eventConfig->modules.size(); ++moduleIndex)
    {
        auto module = eventConfig->modules[moduleIndex];

        u32 *moduleHeader = (u32 *)outp++;
        *moduleHeader = (u32)module->type;

        try
        {
            u32 data = iter.extractU32();

    }

    // split the VMUSB event into event data from one module separated by BERR and EndOfModuleMarker
    while (eventLength)
    {
        u32 data = iter.extractU32();
        if (data == BerrMarker && iter.peekU32() == EndOfModuleMarker)
        {
        }
    }


#if 0
    u16 *bufp = eventBuffer->asU16();

    *bufp++ = eventHeader;

    for (u32 i=0; i<eventLength; ++i)
    {
        u16 data = iter.extractU16();
        *bufp++ = data;
    }

    eventBuffer->used = (eventLength+1) * sizeof(u16);
    //qDebug("eventBuffer ready, usedSize=%u, bytes left in readBuffer=%u", eventBuffer->used, iter.bytesLeft());
    //emit eventReady(eventBuffer.release());
    addFreeBuffer(eventBuffer.release());
#endif
}

void VMUSBBufferProcessor::addFreeBuffer(DataBuffer *buffer)
{
    m_context->getFreeBuffers()->enqueue(buffer);
    //qDebug() << __PRETTY_FUNCTION__ << m_context->getFreeBuffers()->size();
}

DataBuffer* VMUSBBufferProcessor::getFreeBuffer()
{
    auto queue = m_context->getFreeBuffers();
    auto state = m_context->getDAQState();

    while (!queue->size() &&
            (state == DAQState::Running || state == DAQState::Stopping))
    {
        // Avoid fast spinning by blocking if no events are pending
        processQtEvents(QEventLoop::WaitForMoreEvents);
    }
    return queue->dequeue();
}
