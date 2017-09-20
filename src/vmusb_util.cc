#include "vmusb_util.h"
#include "vmusb_constants.h"

using namespace vmusb_constants;

void format_vmusb_buffer(DataBuffer *buffer, QTextStream &out, u64 bufferNumber)
{
    try
    {
        out << "buffer #" << bufferNumber
            << ": bytes=" << buffer->used
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


        tmp = QString("buffer header1=0x%1, numberOfEvents=%2, lastBuffer=%3, cont=%4, mult=%5, buffer#=%6")
            .arg(header1, 8, 16, QLatin1Char('0'))
            .arg(numberOfEvents)
            .arg(lastBuffer)
            .arg(continuousMode)
            .arg(multiBuffer)
            .arg(bufferNumber)
            ;

        out << tmp << endl;

        for (u16 eventIndex = 0; eventIndex < numberOfEvents; ++eventIndex)
        {
            u32 eventHeader = iter.extractShortword();
            u8 stackID          = (eventHeader >> Buffer::StackIDShift) & Buffer::StackIDMask;
            bool partialEvent   = eventHeader & Buffer::ContinuationMask;
            u32 eventLength     = eventHeader & Buffer::EventLengthMask;

            tmp = QString("buffer #%6, event #%5, header=0x%1, stackID=%2, length=%3 shorts, %7 longs, %8 bytes, partial=%4")
                .arg(eventHeader, 8, 16, QLatin1Char('0'))
                .arg(stackID)
                .arg(eventLength)
                .arg(partialEvent)
                .arg(eventIndex)
                .arg(bufferNumber)
                .arg(eventLength / 2)
                .arg(eventLength * 2);
                ;

            out << tmp << endl << "longwords:" << endl;

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

            if (shortwordsLeft)
            {

                out << endl << "shortwords:" << endl;
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

            out << endl << "end of event #" << eventIndex << endl;
        }


        if (iter.bytesLeft())
        {
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
            out << endl;
        }

        out << "end of buffer #" << bufferNumber << endl;
    }
    catch (const end_of_buffer &)
    {
        out << "!!! end of buffer reached unexpectedly !!!" << endl;
    }
}
