#include "data_server_client_lib.h"

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
using std::cout;
using std::cerr;
using std::endl;

namespace
{

class DevClientContext: public ClientContext
{
    public:
        DevClientContext()
        {
            setCallbacks(
                // BeginRunCallback
                [this](const Message &msg, const StreamInfo &streamInfo) {
                    this->beginRun(msg, streamInfo);
                },

                // EventDataCallback
                [this](const Message &msg, int eventIndex,
                       const std::vector<DataSourceContents> &contents) {
                    this->eventData(msg, eventIndex, contents);
                },

                // EndRunCallback
                [this](const Message &msg) { this->endRun(msg); },

                // ErrorCallback
                [this](const Message &msg, const std::exception &e) {
                    this->onError(msg, e);
                });

            // XXX: leftoff here

        }

        bool doQuit() const { return m_quit; }

    private:
        void beginRun(const Message &msg, const StreamInfo &streamInfo);
        void eventData(const Message &msg, int eventIndex,
                       const std::vector<DataSourceContents> &contents);
        void endRun(const Message &msg);
        void onError(const Message &msg, const std::exception &e);

        bool m_quit = false;
};

void DevClientContext::beginRun()

} // end anon namespace

// TODO: setup signal handler for ctrl-c (sigint i think)
// check if sigpipe needs to be handled aswell
int main(int argc, char *argv[])
{
    int sfd = connect_to("localhost", 13801);

    Message msg;
    DevClientContext ctx;

    while (ctx.doQuit())
    {
        read_message(sfd, msg);
        ctx.handleMessage(msg);
    }

    return 0;
}
