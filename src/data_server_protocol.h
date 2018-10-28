#ifndef __MVME_DATA_SERVER_PROTOCOL_H__
#define __MVME_DATA_SERVER_PROTOCOL_H__

#include <cstdint>

namespace mvme
{
namespace data_server
{

enum MessageType: int
{
    Invalid,
    Status,
    BeginRun,
    EndRun,
    BeginEvent,
    EndEvent,
    ModuleData,
    Timetick,

    MessageTypeCount
};

#if 0
struct Message
{
    MessageType type;
    uint32_t size;
    unsigned char *data;
} __attribute__((packed));
#endif

} // end namespace data_server
} // end namespace mvme

#endif /* __MVME_DATA_SERVER_PROTOCOL_H__ */
