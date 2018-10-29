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

    msg.type = read_pod<MessageType>(fd);
    msg.size = read_pod<uint32_t>(fd);

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
/* The following code was taken from the example in `man 3 getaddrinfo' on a
 * linux machine and modified for my needs. */

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

struct DataServerClientContext
{
    json inputInfo;
};

} // end anon namespace

#define DEF_MESSAGE_HANDLER(name) void name(const Message &msg,\
                                            DataServerClientContext &ctx)

typedef DEF_MESSAGE_HANDLER(MessageHandler);

DEF_MESSAGE_HANDLER(hello_handler)
{
    cerr << __PRETTY_FUNCTION__ << "size =" << msg.size << endl;
}

DEF_MESSAGE_HANDLER(begin_run)
{
    cerr << __PRETTY_FUNCTION__ << "size =" << msg.size << endl;

    ctx.inputInfo = json::parse(msg.contents);

    cerr << __PRETTY_FUNCTION__ << ctx.inputInfo.dump(2);
}

DEF_MESSAGE_HANDLER(event_data)
{
    cerr << __PRETTY_FUNCTION__ << "size =" << msg.size << endl;
}

DEF_MESSAGE_HANDLER(end_run)
{
    cerr << __PRETTY_FUNCTION__ << "size =" << msg.size << endl;
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
        bool quit = false;
        Message msg;
        DataServerClientContext ctx;

        while (!quit)
        {
            read_message(sfd, msg);

            if (!msg.isValid())
            {
                throw std::runtime_error("received invalid message structure");
            }

            try
            {
                assert(MessageHandlerTable[msg.type]);
                MessageHandlerTable[msg.type](msg, ctx);

            } catch (const std::exception &e)
            {
                cerr << e.what() << endl;
                return 1;
            }
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
