#include "data_server_protocol.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <system_error>
#include <nlohmann/json.hpp>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using std::cout;
using std::cerr;
using std::endl;
using namespace mvme::data_server;
using json = nlohmann::json;

namespace
{

void read_data(int fd, uint8_t *dest, size_t size)
{
    while (size > 0)
    {
        ssize_t bytesRead = read(fd, dest, size);
        if (bytesRead < 0)
        {
            throw std::system_error(errno, std::generic_category(), "read_data");
        }

        if (bytesRead == 0)
        {
            throw std::runtime_error("server closed connection");
        }

        size -= bytesRead;
        dest += bytesRead;
    }

    assert(size == 0);
}

template<typename T>
T read_pod(int fd)
{
    T result = {};
    read_data(fd, reinterpret_cast<uint8_t *>(&result), sizeof(result));
    return result;
}

static const size_t MaxMessageSize = 10 * 1024 * 1024;

void read_message(int fd, Message &msg)
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

    if (size > MaxMessageSize)
    {
        throw std::runtime_error("Message size exceeds "
                                 + std::to_string(MaxMessageSize) + " bytes");
    }

    if (!msg.isValid())
    {
        throw std::runtime_error("Message type out of range: "
                                 + std::to_string(msg.type));
    }

    msg.contents.resize(size);

    read_data(fd, msg.contents.data(), size);
    assert(msg.isValid());
}

int connect_to(const char *host, const char *service)
{
    // Note (flueke): The following code was taken from the example in `man 3
    // getaddrinfo' on a linux machine and slightly modified.

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
        throw std::runtime_error(gai_strerror(s));
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

    if (rp == NULL) {               /* No address succeeded */
        throw std::runtime_error("Could not connect");
    }

    freeaddrinfo(result);           /* No longer needed */

    return sfd;
}

int connect_to(const char *host, uint16_t port)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(port));
    buffer[sizeof(buffer) - 1] = '\0';

    return connect_to(host, buffer);
}

struct DataSource
{
    std::string name;           // Name of the data source.
    int moduleIndex = -1;       // The index of the module this data source is attached to.
    double lowerLimit = 0.0;    // Lower and upper limits of the values produced by the datasource.
    double upperLimit = 0.0;    //
    uint32_t size = 0u;         // Number of elements in the output of this data source.
    uint32_t bytes = 0u;        // Total number of bytes the output of the data source requires.

};

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

    // Data sources that are part of the readout of this event.
    std::vector<DataSource> dataSources;

    // Offsets for each data source in this event
    std::vector<Offsets> dataSourceOffsets;
};

struct StreamDataDescription
{
    std::vector<EventDataDescription> eventDataDescriptions;
};

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

StreamDataDescription parse_stream_data_description(const json &j)
{
    std::vector<EventDataDescription> result;

    for (const auto &eventJ: j)
    {
        EventDataDescription eds;

        eds.eventIndex = eventJ["eventIndex"];

        for (const auto &dsJ: eventJ["dataSources"])
        {
            DataSource ds;
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

    // Calculate buffer offsets for data sources

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

    return StreamDataDescription { result };
}

VMETree parse_vme_tree(const json &j)
{
    VMETree result;

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

    return result;
}

struct DataServerClientContext
{
    struct RunInfo
    {
        std::string runId;
        bool isReplay = false;
        StreamDataDescription streamInfo;
        VMETree vmeTree;
    };

    RunInfo runInfo;
    json inputInfoJson;
    size_t contentBytesReceived = 0u;
    bool quit = false;
};

class end_of_buffer: public std::exception {};

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

} // end anon namespace

int main(int argc, char *argv[])
{



    return 0;
}
