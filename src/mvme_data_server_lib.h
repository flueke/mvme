#ifndef __MVME_DATA_SERVER_CLIENT_LIB_H__
#define __MVME_DATA_SERVER_CLIENT_LIB_H__

#include "mvme_data_server_proto.h"

#include <cassert>
#include <cstring> // memcpy
#include <functional>
#include <iostream>
#include <system_error>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // getaddrinfo
#else

// POSIX socket API
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <unistd.h>

// header-only json parser library
#include <nlohmann/json.hpp>

namespace mvme
{
namespace data_server
{

using json = nlohmann::json;

// Error handling:
// Most exceptions thrown by the library are a subtype of
// mvme::data_server::exception.
//
// Additionally std::system_error is thrown by the read_*() functions which
// operator on a file descriptor.

// Base exception class used in this library.
struct exception: public std::runtime_error
{
    explicit exception(const std::string &str)
        : std::runtime_error(str)
    {}
};

// Used for errors related to the internal protocol. These errors should not
// happen if client and server are correctly implemented.
struct protocol_error: public exception
{
    using exception::exception;
};

// Data received during a run does not match the description sent at the start
// of the run. This should not happen and is a bug in either the server or the
// client.
struct data_consistency_error: public exception
{
    using exception::exception;
};

struct connection_closed: public exception
{
    using exception::exception;
};

//
// Library init/shutdown. Both return 0 on success.
//
static int lib_init()
{
#ifdef _WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    wVersionRequested = MAKEWORD(2, 1);
    return WSAStartup( wVersionRequested, &wsaData );
#else
    return 0;
#endif
}

static int lib_shutdown()
{
#ifdef _WIN32
    return WSACleanup();
#else
    return 0;
#endif
}


//
// Utilities for reading messages from a file descriptor
//

// Reads exactly size bytes from the file descriptor fd into the destination
// buffer. Interally multiple calls to read() may be performed. If size is 0
// this is a noop.
// Throws std::system_error in case a read fails.
static void read_data(int fd, uint8_t *dest, size_t size)
{
    while (size > 0)
    {
        ssize_t bytesRead = read(fd, dest, size);

        if (bytesRead < 0)
        {
            throw std::system_error(errno, std::system_category(), "read_data");
        }

        if (bytesRead == 0)
        {
            throw connection_closed("remote closed the connection");
        }

        size -= bytesRead;
        dest += bytesRead;
    }

    assert(size == 0);
}

template<typename T>
T read_pod(int fd)
{
    static_assert(std::is_trivial<T>::value, "read_pod() works for trivial types only");

    T result = {};
    read_data(fd, reinterpret_cast<uint8_t *>(&result), sizeof(result));
    return result;
}

// Limit the size of a single incoming message to 10MB. Theoretically 4GB
// messages are possible but in practice messages will be much, much smaller.
// Increase this value if your experiment generates EventData messages that are
// larger or just remove the check in read_message() completely.
static const size_t MaxMessageSize = 10 * 1024 * 1024;

// Read a single Message from the given file descriptor fd into the given msg
// structure.
static void read_message(int fd, Message &msg)
{
    msg.type = MessageType::Invalid;
    msg.contents.clear();

    // Instead of doing two reads for the header like this:
    // msg.type = read_pod<MessageType>(fd);
    // size = read_pod<uint32_t>(fd);
    // ... save one system call by reading the header in one go.
    uint8_t headerBuffer[sizeof(msg.type) + sizeof(uint32_t)];
    read_data(fd, headerBuffer, sizeof(headerBuffer));

    uint32_t size = 0;

    memcpy(&msg.type, headerBuffer,                    sizeof(msg.type));
    memcpy(&size,     headerBuffer + sizeof(msg.type), sizeof(uint32_t));

    if (!msg.isValid())
    {
        throw protocol_error("Received invalid message type: " + std::to_string(msg.type));
    }

    if (size > MaxMessageSize)
    {
        throw protocol_error("Message size exceeds "
                             + std::to_string(MaxMessageSize) + " bytes");
    }

    msg.contents.resize(size);

    read_data(fd, msg.contents.data(), size);
    assert(msg.isValid());
}

// Connects via TCP to the given host and service (the port in our case).
// Returns the socket file descriptor on success, throws if an error occured.
static int connect_to(const char *host, const char *service)
{
    // Note (flueke): The following code was taken from the example in `man 3
    // getaddrinfo' on a linux machine and modified to throw on error.

    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd = -1, s, j;
    size_t len;
    ssize_t nread;

    /* Obtain address(es) matching host/port */

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Stream socket (TCP) */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;           /* Any protocol */

    s = getaddrinfo(host, service, &hints, &result);
    if (s != 0) {
        throw exception(gai_strerror(s));
    }

    /* getaddrinfo() returns a list of address structures.
       Try each address until we successfully connect(2).
       If socket(2) (or connect(2)) fails, we (close the socket
       and) try the next address. */

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                     rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */

        close(sfd);
    }

    freeaddrinfo(result);           /* No longer needed */

    if (rp == NULL) {               /* No address succeeded */
        throw exception("Could not connect");
    }

    return sfd;
}

// Same as above but taking a port number instead of a string.
static int connect_to(const char *host, uint16_t port)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(port));
    buffer[sizeof(buffer) - 1] = '\0';

    return connect_to(host, buffer);
}

// Description of a datasource contained in the data stream. Multiple data
// sources can be part of the same event and multiple datasources can be
// attached to the same vme module.
// Currently the only data type a datasource can contain is an array of double
// values, so no type information is stored here yet.
struct DataSourceDescription
{
    std::string name;           // Name of the datasource.
    int moduleIndex = -1;       // The index of the module this datasource is attached to.
    double lowerLimit = 0.0;    // Lower and upper limits of the values produced by the datasource.
    double upperLimit = 0.0;    //
    uint32_t size = 0u;         // Number of elements in the output array of this datasource.
    uint32_t bytes = 0u;        // Total number of bytes the output of the datasource requires.
};

// Description of the data layout for one mvme event. This contains all the
// datasources attached to the event.
// Additionally byte offsets into the contents of an EventData-type message are
// calculated and stored for each datasource.
struct EventDataDescription
{
    // Offsets for a datasource from the beginning of the message contents in
    // bytes.
    struct Offsets
    {
        uint32_t index = 0;         // the index value of this datasource (consistency check)
        uint32_t bytes = 0;         // index + 4 (consistency check with DataSource::bytes)
        uint32_t dataBegin = 0;     // bytes + 4
        uint32_t dataEnd = 0;       // dataBegin + DataSource::bytes
    };

    int eventIndex = -1;

    // Datasources that are part of the readout of this event.
    std::vector<DataSourceDescription> dataSources;

    // Offsets for each datasource in this event
    std::vector<Offsets> dataSourceOffsets;
};

// Per event data layout descriptions
using EventDataDescriptions = std::vector<EventDataDescription>;

// Structures describing the VME tree as configured inside mvme. This contains
// the hierarchy of events and modules.

struct VMEModule
{
    int moduleIndex;
    std::string name;
    std::string type;
};

struct VMEEvent
{
    int eventIndex = -1;
    std::string name;
    std::vector<VMEModule> modules;
};

struct VMETree
{
    std::vector<VMEEvent> events;
};

struct StreamInfo
{
    EventDataDescriptions eventDescriptions;
    VMETree vmeTree;
    std::string runId;
    bool isReplay = false;
    json infoJson;
};

static EventDataDescriptions parse_stream_data_description(const json &j)
{
    EventDataDescriptions result;

    try
    {
        for (const auto &eventJ: j)
        {
            EventDataDescription eds;
            eds.eventIndex = eventJ["eventIndex"];

            for (const auto &dsJ: eventJ["dataSources"])
            {
                DataSourceDescription ds;
                ds.name = dsJ["name"];
                ds.moduleIndex = dsJ["moduleIndex"];
                ds.size  = dsJ["output_size"];
                ds.bytes = dsJ["output_bytes"];
                ds.lowerLimit = dsJ["output_lowerLimit"];
                ds.upperLimit = dsJ["output_upperLimit"];
                eds.dataSources.emplace_back(ds);
            }
            result.emplace_back(eds);
        }

        // Calculate buffer offsets for datasources
        for (auto &edd: result)
        {
            // One Offset structure for each datasource
            edd.dataSourceOffsets.reserve(edd.dataSources.size());

            // Each message starts with a 4 byte event index.
            uint32_t currentOffset = sizeof(uint32_t);

            for (const auto &ds: edd.dataSources)
            {
                EventDataDescription::Offsets offsets;

                offsets.index = currentOffset; currentOffset += sizeof(uint32_t);
                offsets.bytes = currentOffset; currentOffset += sizeof(uint32_t);
                offsets.dataBegin = currentOffset;
                offsets.dataEnd = currentOffset + ds.bytes;
                currentOffset = offsets.dataEnd;
                edd.dataSourceOffsets.emplace_back(offsets);
            }

            assert(edd.dataSources.size() == edd.dataSourceOffsets.size());
        }
    }
    catch (const json::exception &e)
    {
        throw protocol_error(e.what());
    }

    return result;
}

static VMETree parse_vme_tree(const json &j)
{
    VMETree result;

    try
    {

        for (const auto &eventJ: j)
        {
            VMEEvent event;
            event.name = eventJ["name"];
            event.eventIndex = eventJ["eventIndex"];

            for (const auto &moduleJ: eventJ["modules"])
            {
                VMEModule module;
                module.moduleIndex = moduleJ["moduleIndex"];
                module.name = moduleJ["name"];
                module.type = moduleJ["type"];
                event.modules.emplace_back(module);
            }
            result.events.emplace_back(event);
        }
    }
    catch (const json::exception &e)
    {
        throw protocol_error(e.what());
    }

    return result;
}

static StreamInfo parse_stream_info(const json &j)
{
    StreamInfo result;

    try
    {

        result.infoJson = j;
        result.runId = j["runId"];
        result.eventDescriptions = parse_stream_data_description(j["eventDataSources"]);
        result.vmeTree = parse_vme_tree(j["vmeTree"]);

        for (const auto &edd: result.eventDescriptions)
        {
            if (!(0 <= edd.eventIndex
                  && static_cast<size_t>(edd.eventIndex) < result.vmeTree.events.size()))
            {
                throw protocol_error("data description eventIndex out of range");
            }
        }
    }
    catch (const json::exception &e)
    {
        throw protocol_error(e.what());
    }

    return result;
}

template<typename T, typename U>
bool range_check_lt(const T *ptr, const U *end)
{
    return (reinterpret_cast<const uint8_t *>(ptr)
            < reinterpret_cast<const uint8_t *>(end));
}

template<typename T, typename U>
bool range_check_le(const T *ptr, const U *end)
{
    return (reinterpret_cast<const uint8_t *>(ptr)
            <= reinterpret_cast<const uint8_t *>(end));
}

struct DataSourceContents
{
    const uint32_t *index = nullptr;
    const uint32_t *bytes = nullptr;
    const double *dataBegin = nullptr;
    const double *dataEnd = nullptr;
};

class Parser
{
    public:
        void handleMessage(const Message &msg);
        void reset();
        virtual ~Parser() {}

    protected:
        virtual void serverInfo(const Message &msg, const json &info) = 0;

        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) = 0;

        virtual void eventData(const Message &msg, int eventIndex,
                               const std::vector<DataSourceContents> &contents) = 0;

        virtual void endRun(const Message &msg) = 0;

        virtual void error(const Message &msg, const std::exception &e) = 0;

    private:
        void _serverInfo(const Message &msg);
        void _beginRun(const Message &msg);
        void _eventData(const Message &msg);
        void _endRun(const Message &msg);

        MessageType m_prevMsgType = MessageType::Invalid;
        StreamInfo m_streamInfo;
        std::vector<DataSourceContents> m_contentsVec;
};

void Parser::reset()
{
    m_prevMsgType = MessageType::Invalid;
    m_streamInfo = {};
    m_contentsVec.clear();
}

void Parser::handleMessage(const Message &msg)
{
    try
    {
        if (!is_valid_transition(m_prevMsgType, msg.type))
        {
            throw protocol_error("Unexpected message sequence: '"
                                 + to_string(m_prevMsgType)
                                 + "' -> '"
                                 + to_string(msg.type)
                                 + "'");
        }

        switch (msg.type)
        {
            case ServerInfo: _serverInfo(msg); break;
            case BeginRun: _beginRun(msg); break;
            case EventData: _eventData(msg); break;
            case EndRun: _endRun(msg); break;
            default: assert(false); break;
        }
        m_prevMsgType = msg.type;
    }
    catch (const std::exception &e)
    {
        try
        {
            // Invoke user error handler with the current state
            error(msg, e);

            // Reset to initial, clean state
            reset();
        }
        catch (const std::exception &)
        {
            // Handle exception thrown by the user error handler code: reset to
            // clean state and rethrow.
            reset();
            throw;
        }
    }
}

void Parser::_serverInfo(const Message &msg)
{
    auto infoJson = json::parse(msg.contents);
    serverInfo(msg, infoJson);
}

void Parser::_beginRun(const Message &msg)
{
    auto infoJson = json::parse(msg.contents);
    m_streamInfo = parse_stream_info(infoJson);
    beginRun(msg, m_streamInfo);
}

void Parser::_eventData(const Message &msg)
{
    if (msg.contents.size() == 0u)
        throw protocol_error("Received empty EventData message");

    const uint8_t *contentsBegin = msg.contents.data();
    const uint8_t *contentsEnd   = msg.contents.data() + msg.contents.size();
    auto eventIndex = *(reinterpret_cast<const uint32_t *>(contentsBegin));

    if (eventIndex >= m_streamInfo.eventDescriptions.size())
        throw data_consistency_error("eventIndex out of range");

    const EventDataDescription &edd = m_streamInfo.eventDescriptions[eventIndex];
    assert(edd.dataSources.size() == edd.dataSourceOffsets.size());
    const size_t dataSourceCount = edd.dataSources.size();

    m_contentsVec.resize(dataSourceCount);

    for (size_t dsIndex = 0; dsIndex < dataSourceCount; dsIndex++)
    {
        const auto &ds = edd.dataSources[dsIndex];
        const auto &dsOffsets = edd.dataSourceOffsets[dsIndex];

        // Use precalculated offsets to setup pointers into the current message
        // buffer.
        auto dsIndexCheck = reinterpret_cast<const uint32_t *>(contentsBegin + dsOffsets.index);
        auto dsBytesCheck = reinterpret_cast<const uint32_t *>(contentsBegin + dsOffsets.bytes);
        auto dataBegin = reinterpret_cast<const double *>(contentsBegin + dsOffsets.dataBegin);
        auto dataEnd   = reinterpret_cast<const double *>(contentsBegin + dsOffsets.dataEnd);

        if (!range_check_lt(dsIndexCheck, contentsEnd)
            || !range_check_lt(dsBytesCheck, contentsEnd)
            || !range_check_lt(dataBegin, contentsEnd)
            || !range_check_le(dataEnd, contentsEnd)
           )
        {
            throw data_consistency_error("EventData message contents too small");
        }

        // Compare the received index number with the one from the data
        // description.
        if (*dsIndexCheck != dsIndex)
            throw data_consistency_error("dsIndexCheck failed");

        // Same as above but with the number of bytes in the datasource.
        if (*dsBytesCheck != ds.bytes)
            throw data_consistency_error("dsBytesCheck failed");

        // Store pointers for the datasource
        m_contentsVec[dsIndex] = { dsIndexCheck, dsBytesCheck, dataBegin, dataEnd };

#if 0
        cout << "eventIndex=" << eventIndex << ", dsIndex=" << dsIndex
            << ", dsName=" << ds.name << ", dsSize=" << ds.size
            << ", ds.ll=" << ds.lowerLimit << ", ds.ul=" << ds.upperLimit
            << ": " << endl << "  ";

        for (const double *it = dataBegin; it < dataEnd; it++)
        {
            cout << *it << ", ";
        }

        cout << endl;
#endif
    }

    // Call the virtual data handler
    eventData(msg, eventIndex, m_contentsVec);

    // This is done as a precaution because the raw pointers are only valid as
    // long as the caller-owned Message object is alive.
    m_contentsVec.clear();
}

void Parser::_endRun(const Message &msg)
{
    endRun(msg);
}

} // end namespace data_server
} // end namespace mvme


#endif /* __MVME_DATA_SERVER_CLIENT_LIB_H__ */
