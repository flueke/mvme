/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __MVME_DATA_SERVER_CLIENT_LIB_H__
#define __MVME_DATA_SERVER_CLIENT_LIB_H__

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // getaddrinfo
#else
// POSIX socket API
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <cassert>
#include <cstring> // memcpy
#include <functional>
#include <iostream>
#include <system_error>

#include <unistd.h>

// header-only json parser library
#include <nlohmann/json.hpp>

#include "event_server_proto.h"

namespace mvme
{
namespace event_server
{

using json = nlohmann::json;

// Error handling:
// Most exceptions thrown by the library are a subtype of
// mvme::event_server::exception.
//
// Additionally std::system_error is thrown by the read_*() functions which
// operate on file descriptors.

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

struct end_of_buffer: public std::exception
{
    using exception::exception;
};

//
// Helper for raw buffer iteration
//
struct BufferIterator
{
    uint8_t *data = nullptr;
    uint8_t *buffp = nullptr;
    uint8_t *endp = nullptr;
    size_t size = 0;

    BufferIterator()
    {}

    BufferIterator(uint8_t *data, size_t size)
        : data(data)
        , buffp(data)
        , endp(data + size)
        , size(size)
    {}

    template <typename T>
    inline T extract()
    {
        if (buffp + sizeof(T) > endp)
            throw end_of_buffer();

        T ret = *reinterpret_cast<T *>(buffp);
        buffp += sizeof(T);
        return ret;
    }

    inline uint8_t extractU8() { return extract<uint8_t>(); }
    inline uint16_t extractU16() { return extract<uint16_t>(); }
    inline uint32_t extractU32() { return extract<uint32_t>(); }

    template <typename T>
    inline T peek() const
    {
        if (buffp + sizeof(T) > endp)
            throw end_of_buffer();

        T ret = *reinterpret_cast<T *>(buffp);
        return ret;
    }

    inline uint8_t peekU8() const { return peek<uint8_t>(); }
    inline uint16_t peekU16() const { return peek<uint16_t>(); }
    inline uint32_t peekU32() const { return peek<uint32_t>(); }

    // Pushes a value onto the back of the buffer. Returns a pointer to the
    // newly pushed value.
    template <typename T>
    inline T *push(T value)
    {
        static_assert(std::is_trivial<T>::value, "push<T>() works for trivial types only");

        if (buffp + sizeof(T) > endp)
            throw end_of_buffer();

        T *ret = reinterpret_cast<T *>(buffp);
        buffp += sizeof(T);
        *ret = value;
        return ret;
    }

    inline uint32_t bytesLeft() const
    {
        return endp - buffp;
    }

    inline uint8_t  *asU8()  { return reinterpret_cast<uint8_t *>(buffp); }
    inline uint16_t *asU16() { return reinterpret_cast<uint16_t *>(buffp); }
    inline uint32_t *asU32() { return reinterpret_cast<uint32_t *>(buffp); }

    inline void skip(size_t bytes)
    {
        buffp += bytes;
        if (buffp > endp)
            buffp = endp;
    }

    inline void skip(size_t width, size_t count)
    {
        skip(width * count);
    }

    inline bool atEnd() const { return buffp == endp; }

    inline void rewind() { buffp = data; }
    inline bool isEmpty() const { return size == 0; }
    inline bool isNull() const { return !data; }
    inline size_t used() const { return buffp - data; }
};

//
// Library init/shutdown. Both return 0 on success.
//
__attribute__((__used__))
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

__attribute__((__used__))
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
// Throws std::system_error in case a read fails and connect_closed if the
// file descriptor was closed
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

// Limit the size of a single incoming message to 100MB. Theoretically 4GB
// messages are possible but in practice messages will be much, much smaller.
// Increase this value if your experiment generates EventData messages that are
// larger or just remove the check in read_message() completely.
static const size_t MaxMessageSize = 100 * 1024 * 1024;

// Read a single Message from the given file descriptor fd into the given msg
// structure.
__attribute__((__used__))
static void read_message(int fd, Message &msg)
{
    msg.type = MessageType::Invalid;
    msg.contents.clear();

    // Read message type and size.
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
        throw protocol_error("Message size exceeds maximum size of "
                             + std::to_string(MaxMessageSize) + " bytes");
    }

    msg.contents.resize(size);

    read_data(fd, msg.contents.data(), size);
    assert(msg.isValid());
}

// Connects via TCP to the given host and service (the port in our case).
// Returns the socket file descriptor on success, throws if an error occured.
inline int connect_to(const char *host, const char *service)
{
    // Note (flueke): The following code was taken from the example in `man 3
    // getaddrinfo' on a linux machine and modified to throw on error.

    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd = -1, s;

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
inline int connect_to(const char *host, uint16_t port)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(port));
    buffer[sizeof(buffer) - 1] = '\0';

    return connect_to(host, buffer);
}

//
// Network data storage utils
//

enum class StorageType
{
    st_uint8_t,
    st_uint16_t,
    st_uint32_t,
    st_uint64_t,
};

static std::string to_string(const StorageType &st)
{
    switch (st)
    {
        case StorageType::st_uint8_t: return "uint8_t";
        case StorageType::st_uint16_t: return "uint16_t";
        case StorageType::st_uint32_t: return "uint32_t";
        case StorageType::st_uint64_t: return "uint64_t";
    }
    return {};
}

static StorageType storage_type_from_string(const std::string &str)
{
    auto result = StorageType::st_uint64_t;

    if (str == "uint8_t") result = StorageType::st_uint8_t;
    else if (str == "uint16_t") result = StorageType::st_uint16_t;
    else if (str == "uint32_t") result = StorageType::st_uint32_t;
    else if (str == "uint64_t") result = StorageType::st_uint64_t;

    return result;
}

static size_t get_storage_type_size(const StorageType &st)
{
    switch (st)
    {
        case StorageType::st_uint8_t: return sizeof(uint8_t);
        case StorageType::st_uint16_t: return sizeof(uint16_t);
        case StorageType::st_uint32_t: return sizeof(uint32_t);
        case StorageType::st_uint64_t: return sizeof(uint64_t);
    }

    return 0;
}

/* Reads the next value from the Storage buffer according to the given
 * StorageType and returns the value converted to the result type R. */
template <typename R>
R read_storage(StorageType st, const uint8_t *buffer)
{
    R result = {};

    switch (st)
    {
        case StorageType::st_uint8_t:
            result = *reinterpret_cast<const uint8_t *>(buffer);
            break;
        case StorageType::st_uint16_t:
            result = *reinterpret_cast<const uint16_t *>(buffer);
            break;
        case StorageType::st_uint32_t:
            result = *reinterpret_cast<const uint32_t *>(buffer);
            break;
        case StorageType::st_uint64_t:
            result = *reinterpret_cast<const uint64_t *>(buffer);
            break;
    }

    return result;
}


// Description of a datasource contained in the data stream.
// Multiple datasources can be attached to the same vme module and thus become
// a part of the same readout event.
struct DataSourceDescription
{
    std::string name;           // Name of the datasource.
    int moduleIndex = -1;       // The index of the module this datasource is attached to.
    uint32_t size = 0u;         // Number of elements in the output array of this datasource.
    double lowerLimit = 0.0;    // Lower and upper limits of the values produced by the datasource.
    double upperLimit = 0.0;
    uint8_t bits = 0u;          // Number of data bits the data source produces.

    StorageType indexType;      // Data types used to store the index and data values during network
    StorageType valueType;      // transfer.

    // Optional list of names of individual array elements. This does not have
    // to have the same length as the size of this datasource as not all
    // parameters have to be named.
    std::vector<std::string> paramNames;
};

// Description of the data layout for one mvme event. This contains
// descriptions for all the datasources attached to the event.
struct EventDataDescription
{
    int eventIndex = -1;

    // Datasources that are part of the readout of this event.
    std::vector<DataSourceDescription> dataSources;
};

// Per event data layout descriptions
using EventDataDescriptions = std::vector<EventDataDescription>;

// Structures describing the VME tree as configured inside mvme. This contains
// the hierarchy of events and modules.

struct VMEModule
{
    int moduleIndex = -1;
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
    EventDataDescriptions eventDataDescriptions;
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

            if (eventJ.count("dataSources"))
            {
                for (const auto &dsj: eventJ["dataSources"])
                {
                    DataSourceDescription dsd;
                    dsd.name = dsj["name"];
                    dsd.size = dsj["size"];
                    dsd.lowerLimit = dsj["lowerLimit"];
                    dsd.upperLimit = dsj["upperLimit"];
                    dsd.bits = dsj["bits"];
                    dsd.indexType = storage_type_from_string(dsj["indexType"]);
                    dsd.valueType = storage_type_from_string(dsj["valueType"]);
                    dsd.moduleIndex = dsj["moduleIndex"];

                    for (const auto &namej: dsj["paramNames"])
                        dsd.paramNames.push_back(namej);

                    eds.dataSources.emplace_back(dsd);
                }
            }
            result.emplace_back(eds);
        }
    }
    catch (const json::exception &e)
    {
        throw protocol_error(e.what());
    }

    return result;
}

inline json to_json(const EventDataDescriptions &edds)
{
    json result;

    for (auto &edd: edds)
    {
        json eddj;
        eddj["eventIndex"] = edd.eventIndex;

        for (auto &dsd: edd.dataSources)
        {
            json dsj;

            dsj["name"] = dsd.name;
            dsj["size"] = dsd.size;
            dsj["lowerLimit"] = dsd.lowerLimit;
            dsj["upperLimit"] = dsd.upperLimit;
            dsj["bits"] = static_cast<uint32_t>(dsd.bits);
            dsj["indexType"] = to_string(dsd.indexType);
            dsj["valueType"] = to_string(dsd.valueType);
            dsj["moduleIndex"] = dsd.moduleIndex;
            json namesj;
            for (auto &name: dsd.paramNames)
                namesj.push_back(name);
            dsj["paramNames"] = namesj;

            eddj["dataSources"].push_back(dsj);
        }

        result.push_back(eddj);
    }

    return result;
}

inline json to_json(const VMETree &vmeTree)
{
    json result;

    for (auto &event: vmeTree.events)
    {
        json ej;
        ej["eventIndex"] = event.eventIndex;
        ej["name"] = event.name;

        for (auto &module: event.modules)
        {
            json mj;
            mj["moduleIndex"] = module.moduleIndex;
            mj["name"] = module.name;
            mj["type"] = module.type;
            ej["modules"].push_back(mj);
        }
        result.push_back(ej);
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
        result.eventDataDescriptions = parse_stream_data_description(j["eventDataSources"]);
        result.vmeTree = parse_vme_tree(j["vmeTree"]);

        for (const auto &edd: result.eventDataDescriptions)
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

// Helper to deal with incoming packed, indexed arrays.
// The firstIndex member points to a buffer containing packed (index, value)
// pairs with their data types specified in the members indexType and
// valueType. The count field contains the number of packed pairs available in
// the buffer.
struct DataSourceContents
{
    StorageType indexType;
    StorageType valueType;
    uint16_t count = 0;
    const uint8_t *firstIndex = nullptr;
};

// Returns the size of one element of the packed (index, value) array described
// by the given DataSourceContents structure.
static size_t get_entry_size(const DataSourceContents &dsc)
{
    return get_storage_type_size(dsc.indexType)
        + get_storage_type_size(dsc.valueType);
}

// Returns a pointer to "one past the last element" of the given data source.
inline const uint8_t *get_end_pointer(const DataSourceContents &dsc)
{
    return dsc.firstIndex + get_entry_size(dsc) * dsc.count;
}

template <typename Out>
void print(Out &out, const DataSourceContents &dsc)
{
    const auto entrySize = get_entry_size(dsc);

    if (dsc.count == 0)
    {
        out << std::endl;
        return;
    }

    for (auto entryIndex = 0; entryIndex < dsc.count; entryIndex++)
    {
        const uint8_t *indexPtr = dsc.firstIndex + entryIndex * entrySize;
        const uint8_t *valuePtr = indexPtr + get_storage_type_size(dsc.indexType);

        uint32_t index = read_storage<uint32_t>(dsc.indexType, indexPtr);
        uint64_t value = read_storage<uint64_t>(dsc.valueType, valuePtr);

        bool last = (entryIndex == dsc.count - 1);

        out << "(" << index << ", " << value << ")";
        if (last) out << std::endl;
        else out << ", ";
    }
}

// Base class for event_server clients.
class Client
{
    public:
        void handleMessage(const Message &msg);
        void reset();
        const StreamInfo &getStreamInfo() const { return m_streamInfo; }
        virtual ~Client() {}

    protected:
        virtual void serverInfo(const Message &msg, const json &info) = 0;

        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) = 0;

        virtual void eventData(const Message &msg, int eventIndex,
                               const std::vector<DataSourceContents> &contents) = 0;

        virtual void endRun(const Message &msg, const json &info) = 0;

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

inline void Client::reset()
{
    m_prevMsgType = MessageType::Invalid;
    m_streamInfo = {};
    m_contentsVec.clear();
}

inline void Client::handleMessage(const Message &msg)
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
#if 1
        throw;
#else
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
#endif
    }
}

inline void Client::_serverInfo(const Message &msg)
{
    auto infoJson = json::parse(msg.contents);
    serverInfo(msg, infoJson);
}

inline void Client::_beginRun(const Message &msg)
{
    auto infoJson = json::parse(msg.contents);

    //std::cout << __FUNCTION__ << ": streamInfo JSON data:" << std::endl
    //    << infoJson.dump(2) << std::endl;

    m_streamInfo = parse_stream_info(infoJson);
    beginRun(msg, m_streamInfo);
}

inline void Client::_eventData(const Message &msg)
{
    if (msg.contents.size() == 0u)
        throw protocol_error("Received empty EventData message");

    try
    {
        uint8_t *contentsBegin = const_cast<uint8_t *>(msg.contents.data());

        BufferIterator ci(contentsBegin, msg.contents.size());
        uint8_t eventIndex = ci.extractU8();

        if (eventIndex >= m_streamInfo.eventDataDescriptions.size())
            throw data_consistency_error("eventIndex out of range");

        const auto &edd = m_streamInfo.eventDataDescriptions[eventIndex];
        m_contentsVec.resize(edd.dataSources.size());

        for (size_t dsIndex = 0; dsIndex < edd.dataSources.size(); dsIndex++)
        {
            uint8_t indexCheck = ci.extractU8();

            if (indexCheck != dsIndex)
            {
                throw data_consistency_error(
                    "Wrong dataSourceIndex in EventData message, expected "
                    + std::to_string(dsIndex) + ", got " + std::to_string(indexCheck));
            }

            uint16_t elementCount = ci.extractU16();
            const auto &dsd = edd.dataSources[dsIndex];

            DataSourceContents dsc;
            dsc.indexType = dsd.indexType;
            dsc.valueType = dsd.valueType;
            dsc.count = elementCount;
            dsc.firstIndex = ci.buffp;
            m_contentsVec[dsIndex] = dsc;

            // Skip over the (index, value) pairs to make the iterator point to the
            // next dataSourceIndex.
            ci.skip(elementCount * get_entry_size(dsc));
        }

        // Call the virtual data handler
        eventData(msg, eventIndex, m_contentsVec);

        // This is done as a precaution because the raw pointers are only valid as
        // long as the caller-owned Message object is alive.
        m_contentsVec.clear();
    }
    catch (const end_of_buffer &)
    {
        throw data_consistency_error(
            "Unexpectedly hit end of buffer while parsing EventData message");
    }
}

inline void Client::_endRun(const Message &msg)
{
    auto infoJson = json::parse(msg.contents);
    endRun(msg, infoJson);
}

} // end namespace event_server
} // end namespace mvme


#endif /* __MVME_DATA_SERVER_CLIENT_LIB_H__ */
