#ifndef __MVME_DATA_SERVER_PROTOCOL_H__
#define __MVME_DATA_SERVER_PROTOCOL_H__

#include <cstdint>

namespace mvme
{
namespace data_server
{

enum MessageType: int
{
    Invalid = 0,
    Hello,
    BeginRun,
    EventData,
    EndRun,

    MessageTypeCount
};

#if 0
struct MessageHeader
{
    MessageType type;
    uint32_t size;
}; // __attribute__((packed));
#endif

} // end namespace data_server
} // end namespace mvme

#endif /* __MVME_DATA_SERVER_PROTOCOL_H__ */
