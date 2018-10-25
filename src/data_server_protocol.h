#ifndef __MVME_DATA_SERVER_PROTOCOL_H__
#define __MVME_DATA_SERVER_PROTOCOL_H__

#include <cstdint>

namespace mvme
{

enum MessageType: uint32_t
{
    InitialInfo,
    BeginRun,
    EndRun,
    BeginEvent,
    EndEvent,
    ModuleData,
    Timetick,

    MessageTypeCount
};

struct MessageHeader
{
    uint32_t size;
    MessageType type;
} __attribute__((packed)); /* No padding wanted. */

} // end namespace mvme

#endif /* __MVME_DATA_SERVER_PROTOCOL_H__ */
