#include "data_server_protocol.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <system_error>
#include <nlohmann/json.hpp>

#include <unistd.h>

using namespace mvme::data_server;
using std::cerr;
using std::endl;
using json = nlohmann::json;

struct DataServerClientContext
{
};

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

namespace
{

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
}

template<typename T>
T read_pod(int fd)
{
    T result = {};
    read_data(fd, reinterpret_cast<char *>(&result), sizeof(result));
    return result;
}

void read_message(int fd, Message &msg)
{
    msg.type = MessageType::Invalid;
    msg.size = 0;
    msg.contents.clear();

    msg.type = read_pod<MessageType>(fd);
    msg.size = read_pod<uint32_t>(fd);

    if (!(0 < msg.type && msg.type < MessageType::MessageTypeCount))
    {
        std::ostringstream ss;
        ss << "Message type out of range: " << msg.type;
        throw std::runtime_error(ss.str());
    }

    msg.contents.resize(msg.size);

    read_data(fd, msg.contents.data(), msg.size);
}

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

#if 0
    ctx.inputInfo = {};

    QJsonParseError parseError;
    auto doc(QJsonDocument::fromJson(msg.contents, &parseError));

    if (parseError.error != QJsonParseError::NoError)
    {
        throw std::runtime_error("begin_run: error parsing inputInfo json: "
                                 + parseError.errorString().toStdString());
    }

    ctx.inputInfo = doc.object();

    qDebug() << __PRETTY_FUNCTION__ << "received inputInfo:" << endl << doc.toJson();
#endif
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
    // Seems to be required for hostname lookups to work.
    QCoreApplication app(argc, argv);;

    QTcpSocket socket;

    socket.connectToHost("localhost", 13801);
    if (!socket.waitForConnected())
    {
        assert(!"waitForConnected");
        return 1;
    }

    assert(socket.state() == QAbstractSocket::ConnectedState);

    QDataStream sockStream(&socket);
    DataServerClientContext ctx;
#endif

    struct hostent *server = gethostbyname("localhost");
    int port   = 13801;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

#if 0
    try
    {
#endif

        bool quit = false;
        Message msg;

        while (!quit)
        {
            read_one_message(sockStream, msg);

            if (sockStream.status() != QDataStream::Ok)
            {
                cerr << sockStream.status() << endl;
                assert(!"socketStream.status() is not ok");
                return 1;
            }

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
