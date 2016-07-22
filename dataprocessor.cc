#include "dataprocessor.h"
#include "databuffer.h"
#include "mvme_context.h"
#include "vmusb.h"
#include <QDebug>
#include <QThread>

using namespace VMUSBConstants;

struct DataProcessorPrivate
{
    MVMEContext *context;
};

DataProcessor::DataProcessor(MVMEContext *context, QObject *parent)
    : QObject(parent)
    , m_d(new DataProcessorPrivate)
{
    m_d->context = context;
}

void DataProcessor::processBuffer(DataBuffer *buffer)
{
    auto vmusb = dynamic_cast<VMUSB *>(m_d->context->getController());
    BufferIterator::Alignment align = (vmusb->getMode() & GlobalMode::Align32Mask) ?
        BufferIterator::Align32 : BufferIterator::Align16;
    BufferIterator iter(buffer->data, buffer->size, align);

    try
    {
        u32 header1 = buffer.extractWord();

        bool lastBuffer     = header1 & Buffer::LastBufferMask;
        bool scalerBuffer   = header1 & Buffer::IsScalerBufferMask;
        bool continuousMode = header1 & Buffer::ContinuationMask;
        bool multiBuffer    = header1 & Buffer::MultiBufferMask;
        u16 numberOfEvents  = header1 & Buffer::NumberOfEventsMask;

        if (lastBuffer || scalerBuffer || continuousMode || multiBuffer)
        {
            qDebug("header1: lastBuffer=%d, scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
                    lastBuffer, scalerBuffer, continuousMode, multiBuffer, numberOfEvents);
        }

        if (vmusb->getMode() & GlobalMode::HeaderOptMask)
        {
            u32 header2 = buffer.extractWord();
            u16 numberOfWords = header2 & Buffer::NumberOfWordsMask;
            //qDebug("header2: numberOfWords=%u", numberOfWords);
        }

        for (u32 eventIndex=0; eventIndex < numberOfEvents; ++eventIndex)
        {
            u32 eventHeader = buffer.extractWord();

            u8 stackId          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
            bool partialEvent   = eventHeader & Buffer::ContinuationMask;
            u16 eventLength     = eventHeader & Buffer::EventLengthMask;

            u32 dataWordsInEvent = (eventLength / sizeof(u16));
            u32 bytesAfterData  = eventLength % sizeof(u16);

            if (partialEvent || bytesAfterData)
            {
                qDebug("eventHeader=%08x, stackId=%u, partialEvent=%d, eventLength=%u, dataWordsInEvent=%u, bytesAfterData=%u",
                        eventHeader, stackId, partialEvent, eventLength, dataWordsInEvent, bytesAfterData);
            }

            for (u32 i=0; i<dataWordsInEvent; ++i)
            {
                u32 data = buffer.extractU32();
                //qDebug("  data word %d: %08x", i, data);

                if (!scalerBuffer && data != 0xFFFFFFFF && data != 0x00000000)
                {
                }
            }
        }

        u32 bufferTerminator1 = buffer.extractWord();
        u32 bufferTerminator2 = buffer.extractWord();

        //qDebug("bufferTerminator1=%08x, bufferTerminator2=%08x, bytesLeft=%u",
        //        bufferTerminator1, bufferTerminator2, buffer.endp - buffer.buffp);

    }
    catch (const end_of_buffer &)
    {
        qDebug("error: end of buffer reached unexpectedly!");
    }

    emit bufferProcessed(buffer);
}
