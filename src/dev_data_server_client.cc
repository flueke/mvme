#include "data_server_protocol.h"

#include <cassert>
#include <QDataStream>
#include <QTcpSocket>

using namespace mvme::data_server;

int main(int argc, char *argv[])
{
    QTcpSocket socket;

    socket.connectToHost("localhost", 13801);
    if (!socket.waitForConnected())
    {
        assert(!"waitForConnected");
        return 1;
    }

    QDataStream sockStream(&socket);

    bool quit = false;

    while (!quit)
    {
        int msgType = 0;
        uint32_t msgSize;

        sockStream >> msgType >> msgSize;

        switch (static_cast<MessageType>(msgType))
        {
            case Status:
            case BeginRun:
            case EndRun:
            case BeginEvent:
            case EndEvent:
            case ModuleData:
            case Timetick:

            case Invalid:
            default:
                assert(false);
                quit = true;
                break;
        }
    }

    return 0;
}
