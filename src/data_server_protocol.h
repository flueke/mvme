#ifndef __MVME_DATA_SERVER_PROTOCOL_H__
#define __MVME_DATA_SERVER_PROTOCOL_H__

#include <cstddef>
#include <cstdint>
#include <vector>

// Message framing format and message types of the mvme analysis data server
// protocol.
// Details about message contents and how things can be parsed are in a
// separate file (data_server_client_lib.h).
//
// TODO:
// - define "Hello" Handshake information. mvme should send its version
//   information and a version number for this protocol.
// - the client can send an id string which can be used for logging on the
//   server side.
// - send information in end run: total number of events processed on the server side,
//   per event counts, per datasource counts
// - textually describe what the protocol is for and how the information for
//   each message type looks.


namespace mvme
{
namespace data_server
{

// Valid transitions:
// initial   -> Hello
// Hello     -> BeginRun
// BeginRun  -> EventData | EndRun
// EventData -> EventData | EndRun
// EndRun    -> BeginRun
enum MessageType: uint32_t
{
    Invalid = 0,
    Hello,
    BeginRun,
    EventData,
    EndRun,

    MessageTypeCount
};

// Message frame format is (uint32_t type, uint32_t size in bytes)
static const size_t MessageFrameSize = 2 * sizeof(uint32_t);

struct Message
{
    MessageType type = MessageType::Invalid;
    std::vector<uint8_t> contents;

    bool isValid() const
    {
        return (MessageType::Invalid < type
                && type < MessageType::MessageTypeCount);
    }

    size_t size() const { return contents.size(); }
};

} // end namespace data_server
} // end namespace mvme

#endif /* __MVME_DATA_SERVER_PROTOCOL_H__ */
