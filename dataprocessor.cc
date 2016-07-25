#include "dataprocessor.h"
#include "databuffer.h"
#include "mvme_context.h"
#include "vmusb.h"
#include <QDebug>
#include <QThread>
#include <QTime>
#include <QTextStream>

using namespace VMUSBConstants;

void format_vmusb_eventbuffer(DataBuffer *buffer, QTextStream &out)
{
    QString tmp;
    BufferIterator iter(buffer->data, buffer->used, BufferIterator::Align16);

    u32 eventHeader = iter.extractShortword();

    u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
    bool partialEvent   = eventHeader & Buffer::ContinuationMask;
    u32 eventLength     = eventHeader & Buffer::EventLengthMask;

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
