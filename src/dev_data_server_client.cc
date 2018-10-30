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

// NOTE: datasource datatype ignored for now. only arrays of double can be sent/received at the moment

using namespace mvme::data_server;
using std::cerr;
using std::endl;
using json = nlohmann::json;

namespace
{

struct Message
{
    int type = MessageType::Invalid;
    uint32_t size = 0;
    std::vector<char> contents;

    bool isValid() const
    {
        return (0 < type && type < MessageType::MessageTypeCount
                && static_cast<uint32_t>(contents.size()) == size);
    }
};

void read_data(int fd, char *dest, size_t size)
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
    read_data(fd, reinterpret_cast<char *>(&result), sizeof(result));
    return result;
}

static const size_t MaxMessageSize = 10 * 1024 * 1024;

void read_message(int fd, Message &msg)
{
    msg.type = MessageType::Invalid;
    msg.size = 0;
    msg.contents.clear();

    // Instead of doing two reads for the header like this:
    //msg.type = read_pod<MessageType>(fd);
    //msg.size = read_pod<uint32_t>(fd);

    // ... instead save one system call by reading the header in one go.
    char headerBuffer[sizeof(msg.type) + sizeof(msg.size)];
    read_data(fd, headerBuffer, sizeof(headerBuffer));

    memcpy(&msg.type, headerBuffer,                    sizeof(msg.type));
    memcpy(&msg.size, headerBuffer + sizeof(msg.type), sizeof(msg.size));

    if (msg.size > MaxMessageSize)
    {
        throw std::runtime_error("Message size exceeds "
                                 + std::to_string(MaxMessageSize) + " bytes");
    }

    if (!(0 < msg.type && msg.type < MessageType::MessageTypeCount))
    {
        std::ostringstream ss;
        ss << "Message type out of range: " << msg.type;
        throw std::runtime_error(ss.str());
    }

    msg.contents.resize(msg.size);

    read_data(fd, msg.contents.data(), msg.size);
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

// per event data source offsets needed.
// have to walk received event data and adjust pointers
// the offset from the start of the message data can be calculated upfront from
// the beginrun info

struct DataSource
{
    std::string name;           // Name of the data source.
    int moduleIndex = -1;       // The index of the module this data source is attached to.
    double lowerLimit = 0.0;    // Lower and upper limits of the parameter values output
    double upperLimit = 0.0;    // by this data source.
    uint32_t size = 0u;         // Number of elements in the output of this data source.
    uint32_t bytes = 0u;        // Total number of bytes the output of the data source requires.

    struct Offsets
    {
        uint32_t index = 0;
        uint32_t bytes = 0; // index + 4
        uint32_t dataBegin = 0;  // bytes + 4
        uint32_t dataEnd = 0; // dataBegin + size
    };
};

struct EventDataSources
{
    int eventIndex = -1;

    // Data sources that are part of the readout of this event.
    std::vector<DataSource> dataSources;

    // Offset to the data for each DataSource in bytes, counted from the start
    // of message contents. Each entry contains the begin and "one past end" offsets.
    std::vector<std::pair<uint32_t, uint32_t>> dataOffsets;
};

struct StreamInfo
{
    std::vector<EventDataSources> dataSources;
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

std::vector<EventDataSources> parse_data_sources(const json &j)
{
    std::vector<EventDataSources> result;

    for (const auto &eventJ: j)
    {
        EventDataSources eds;

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
    for (auto &eds: result)
    {
        for (const auto &ds: eds.dataSources)
        {
        }
    }

    return result;
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
        VMETree vmeTree;
        StreamInfo streamInfo;
    };

    RunInfo runInfo;
    json inputInfoJson;
    size_t contentBytesReceived = 0u;
    bool quit = false;
};

} // end anon namespace

#define DEF_MESSAGE_HANDLER(name) void name(const Message &msg,\
                                            DataServerClientContext &ctx)

typedef DEF_MESSAGE_HANDLER(MessageHandler);

DEF_MESSAGE_HANDLER(hello_handler)
{
    cerr << __FUNCTION__ << "(): size =" << msg.size << endl;
    ctx.contentBytesReceived = 0u;
    ctx.contentBytesReceived += msg.size;
}

DEF_MESSAGE_HANDLER(begin_run)
{
    cerr << __FUNCTION__ << "(): size =" << msg.size << endl;
    ctx.contentBytesReceived += msg.size;

    ctx.inputInfoJson = json::parse(msg.contents);

    cerr << __FUNCTION__ << endl << ctx.inputInfoJson.dump(2) << endl;

    const auto &j = ctx.inputInfoJson;

    ctx.runInfo.runId = j["runId"];
    ctx.runInfo.vmeTree = parse_vme_tree(j["vmeTree"]);
    ctx.runInfo.eventDataSources = parse_data_sources(j["eventDataSources"]);
}

DEF_MESSAGE_HANDLER(event_data)
{
    //cerr << __FUNCTION__ << "(): size =" << msg.size << endl;
    ctx.contentBytesReceived += msg.size;
}

DEF_MESSAGE_HANDLER(end_run)
{
    cerr << __FUNCTION__ << "(): size =" << msg.size << endl;
    ctx.contentBytesReceived += msg.size;

    cerr << __FUNCTION__ << "(): total content bytes received = "
        << ctx.contentBytesReceived << endl;

    ctx.quit = true;
}

static MessageHandler *
MessageHandlerTable[MessageType::MessageTypeCount] =
{
    nullptr,        // Invalid
    hello_handler,  // Hello
    begin_run,      // BeginRun
    event_data,     // EventData
    end_run         // EndRun
};

int main(int argc, char *argv[])
{
#if 0
    try
    {
#endif
        int sfd = connect_to("localhost", 13801);
        Message msg;
        DataServerClientContext ctx;

        while (!ctx.quit)
        {
            read_message(sfd, msg);

            if (!msg.isValid())
            {
                throw std::runtime_error("received invalid message structure");
            }

#if 0
            try
            {
#endif
                assert(MessageHandlerTable[msg.type]);
                MessageHandlerTable[msg.type](msg, ctx);

#if 0
            } catch (const std::exception &e)
            {
                cerr << e.what() << endl;
                throw;
                //return 1;
            }
#endif
        }
#if 0
    }
    catch (const std::exception &e)
    {
        throw;
    }
#endif

    return 0;
}
