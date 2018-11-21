#include "mvme_data_server_lib.h"

#include <chrono>

using std::cerr;
using std::cout;
using std::endl;
using namespace mvme::data_server;

namespace
{

using ClockType = std::chrono::high_resolution_clock;

struct RunStats
{
    ClockType::time_point tStart;
    ClockType::time_point tEnd;
    size_t messageCount = 0;
    size_t errorCount = 0;
    std::vector<size_t> eventCounts;
    std::vector<size_t> eventDataBytes;
    std::vector<std::vector<size_t>> eventDSBytes;
    size_t totalDataBytes = 0;
    size_t dataMessageCount = 0;
    size_t dataMessageSizeSum = 0;
};

class Context: public mvme::data_server::Parser
{
    public:
        bool doQuit() const { return m_quit; }

    protected:
        virtual void serverInfo(const Message &msg, const json &info) override;

        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) override;

        virtual void eventData(const Message &msg, int eventIndex,
                                const std::vector<DataSourceContents> &contents) override;

        virtual void endRun(const Message &msg) override;

        virtual void error(const Message &msg, const std::exception &e) override;

    private:
        RunStats m_stats;
        bool m_quit = false;
};

void Context::serverInfo(const Message &msg, const json &info)
{
    cout << __FUNCTION__ << ": serverInfo=" << endl << info.dump(2) << endl;
}

void Context::beginRun(const Message &msg, const StreamInfo &streamInfo)
{
    cout << __FUNCTION__ << ": runId=" << streamInfo.runId
        << endl;
    m_stats = {};
    m_stats.messageCount++;

    m_stats.eventCounts.clear();
    m_stats.eventCounts.resize(streamInfo.eventDescriptions.size(), 0u);

    m_stats.eventDataBytes.clear();
    m_stats.eventDataBytes.resize(streamInfo.eventDescriptions.size(), 0u);

    m_stats.eventDSBytes.clear();

    for (auto &edd: streamInfo.eventDescriptions)
    {
        m_stats.eventDSBytes.push_back(std::vector<size_t>(edd.dataSources.size()));
    }

    m_stats.tStart = ClockType::now();
}

void Context::eventData(const Message &msg, int eventIndex,
                        const std::vector<DataSourceContents> &contents)
{
    m_stats.messageCount++;
    m_stats.eventCounts[eventIndex]++;
    m_stats.dataMessageCount++;
    m_stats.dataMessageSizeSum += msg.size();

    for (auto &dsc: contents)
    {
        size_t bytes = (dsc.dataEnd - dsc.dataBegin) * sizeof(double);
        m_stats.totalDataBytes += bytes;
        m_stats.eventDataBytes[eventIndex] += bytes;
        m_stats.eventDSBytes[eventIndex][*dsc.index] += bytes;
    }
}

void Context::endRun(const Message &msg)
{
    m_stats.tEnd = ClockType::now();
    m_stats.messageCount++;

    cout << __FUNCTION__ << " run stats:" << endl;

    std::chrono::duration<float> elapsed = m_stats.tEnd - m_stats.tStart;
    float elapsed_s = elapsed.count();
    float bytesPerSecond = m_stats.totalDataBytes / elapsed_s;
    float MBPerSecond = bytesPerSecond / (1024 * 1024);

    cout << "duration: " << elapsed_s << "s" << endl;
    cout << "data: "
        << m_stats.totalDataBytes << " bytes, "
        << m_stats.totalDataBytes / (1024 * 1024.0) << " MB"
        << endl;
    cout << "rate: "
        << bytesPerSecond << " B/s, "
        << MBPerSecond << " MB/s"
        << endl;

    cout << "dataMessageCount: " << m_stats.dataMessageCount
        << endl
        << "totalMessageSizes: " << m_stats.dataMessageSizeSum
        << endl;

    cout << "eventCounts: ";
    for (size_t ei = 0; ei < m_stats.eventCounts.size(); ei++)
    {
        cout << "ei=" << ei << ", count=" << m_stats.eventCounts[ei] << ", ";
    }
    cout << endl;
}

void Context::error(const Message &msg, const std::exception &e)
{
    m_stats.errorCount++;
}

} // end anon namespace

// TODO: setup signal handler for ctrl-c (sigint i think)
// same as in mvme_root_treewriter_client.cc
int main(int argc, char *argv[])
{
    int res = mvme::data_server::lib_init();
    if (res != 0)
    {
        cerr << "lib_init() failed with code " << res << endl;
        return 1;
    }

    const char *host = "localhost";
    const char *port = "13801";

    cerr << "connecting to " << host << ":" << port << endl;
    int sockfd = connect_to(host, port);

    Message msg;
    Context ctx;

    while (!ctx.doQuit())
    {
        read_message(sockfd, msg);
        ctx.handleMessage(msg);
    }

    mvme::data_server::lib_shutdown();
    return 0;
}
