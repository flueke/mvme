#ifndef __MVME_DATA_SERVER_PROTOCOL_H__
#define __MVME_DATA_SERVER_PROTOCOL_H__

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Message framing format and message types of the mvme analysis data server
// protocol.
// Details about message contents and how things can be parsed are in a
// separate file (data_server_client_lib.h).
//
// TODO:
// - the client can send an id string which can be used for logging on the
//   server side.
// - send information in end run: total number of events processed on the server side,
//   per event counts, per datasource counts
// - textually describe what the protocol is for and how the information for
//   each message type looks.


namespace mvme
{
namespace event_server
{

static const int ProtocolVersion = 1;

// Valid transitions:
// initial      -> ServerInfo
// ServerInfo   -> BeginRun
// BeginRun     -> EventData | EndRun
// EventData    -> EventData | EndRun
// EndRun       -> BeginRun
enum MessageType: uint8_t
{
    Invalid = 0,
    ServerInfo,
    BeginRun,
    EventData,
    EndRun,

    MessageTypeCount
};

// Size of the frame of each message. The message type is followed by a uin32_t
// value specifying the size of the message contents in bytes.
static const size_t MessageFrameSize = sizeof(MessageType) + sizeof(uint32_t);

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

using AllowedTypes = std::array<MessageType, MessageTypeCount>;
using TransitionTable = std::array<AllowedTypes, MessageTypeCount>;

static TransitionTable make_transition_table()
{
    TransitionTable ret;

    ret[MessageType::Invalid]    = { { MessageType::ServerInfo } };
    ret[MessageType::ServerInfo] = { { MessageType::BeginRun } };
    ret[MessageType::BeginRun]   = { { MessageType::EventData, MessageType::EndRun } };
    ret[MessageType::EventData]  = { { MessageType::EventData, MessageType::EndRun } };
    ret[MessageType::EndRun]     = { { MessageType::BeginRun } };

    return ret;
}

static bool is_valid_transition(MessageType prev, MessageType cur)
{
    static const TransitionTable transitions = make_transition_table();

    if (prev < transitions.size())
    {
        const auto &allowed = transitions[prev];
        return (std::find(allowed.begin(), allowed.end(), cur) != allowed.end());
    }
    return false;
}

using MessageTypeStringTable = std::array<std::string, MessageTypeCount>;

static MessageTypeStringTable make_messagetype_stringtable()
{
    MessageTypeStringTable ret;

    ret[MessageType::Invalid]    = "Invalid";
    ret[MessageType::ServerInfo] = "ServerInfo";
    ret[MessageType::BeginRun]   = "BeginRun";
    ret[MessageType::EventData]  = "EventData";
    ret[MessageType::EndRun]     = "EndRun";

    return ret;
}

static const std::string &to_string(MessageType t)
{
    static const auto stringTable = make_messagetype_stringtable();

    return stringTable[t];
}

} // end namespace data_server
} // end namespace mvme

#endif /* __MVME_DATA_SERVER_PROTOCOL_H__ */
