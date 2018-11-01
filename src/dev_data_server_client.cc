#include "data_server_client_lib.h"

using std::cerr;
using std::cout;
using std::endl;
using namespace mvme::data_server;

namespace
{

class Context: public mvme::data_server::Parser
{
    public:
        bool doQuit() const { return m_quit; }

    protected:
        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) override;

        virtual void eventData(const Message &msg, int eventIndex,
                                const std::vector<DataSourceContents> &contents) override;

        virtual void endRun(const Message &msg) override;

        virtual void error(const Message &msg, const std::exception &e) override;

    private:
        bool m_quit = false;
};

void Context::beginRun(const Message &msg, const StreamInfo &streamInfo)
{
    cout << __FUNCTION__ << ": runId=" << streamInfo.runId
        << endl;
}

void Context::eventData(const Message &msg, int eventIndex,
                        const std::vector<DataSourceContents> &contents)
{
}

void Context::endRun(const Message &msg)
{
}

void Context::error(const Message &msg, const std::exception &e) 
{
}

} // end anon namespace

// TODO: setup signal handler for ctrl-c (sigint i think)
// check if sigpipe needs to be handled aswell
int main(int argc, char *argv[])
{
    int sockfd = connect_to("localhost", 13801);

    Message msg;
    Context ctx;

    while (!ctx.doQuit())
    {
        read_message(sockfd, msg);
        ctx.handleMessage(msg);
    }

    return 0;
}
