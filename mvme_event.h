#ifndef UUID_e6b0f2e0_71a4_46c2_8385_8130e63a82ec
#define UUID_e6b0f2e0_71a4_46c2_8385_8130e63a82ec

#include <QTextStream>
#include "databuffer.h"
#include "util.h"

namespace mvme_event
{
    // MVME Event
static const int EventTypeMask  = 0xf;      // 4 bit event type
static const int EventSizeMask  = 0x3ff;    // 10 bit event size in 32 bit words
static const int EventSizeShift = 4;

    // Subevent containing module data
static const int ModuleTypeMask = 0x3f;     // 6 bit module type
static const int SubEventSizeMask = 0x3ff; // 10 bit subevent size in 32 bit words
static const int SubEventSizeShift = 6;
}

void dump_event_buffer(QTextStream &out, const DataBuffer *eventBuffer)
{
    using namespace mvme_event;
    QString buf;
    BufferIterator iter(eventBuffer->data, eventBuffer->used, BufferIterator::Align32);

    u32 eventHeader = iter.extractU32();
    u32 eventType   = eventHeader & EventTypeMask;
    u32 eventSize   = (eventHeader >> EventSizeShift) & EventSizeMask;

    out << buf.sprintf("eventHeader=0x%08x, eventType=%u, eventSize=%u\n",
                           eventHeader, eventType, eventSize);

    while (iter.longwordsLeft())
    {
        u32 subEventHeader = iter.extractU32();
        u32 moduleType = subEventHeader & ModuleTypeMask;
        u32 subEventSize = (subEventHeader >> SubEventSizeShift) & SubEventSizeMask;

        out << buf.sprintf("  subEventHeader=0x%08x, moduleType=%u, subEventSize=%u\n",
                           subEventHeader, moduleType, subEventSize);

        for (u32 i=0; i<subEventSize; ++i)
        {
            u32 subEventData = iter.extractU32();
            out << buf.sprintf("    %u = 0x%08x\n", i, subEventData);
        }
    }
}

#endif
