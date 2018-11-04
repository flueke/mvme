#include "data_server_client_lib.h"

#include <getopt.h>
#include <signal.h>
#include <TFile.h>
#include <TNtupleD.h>
#include <TTree.h>

using std::cerr;
using std::cout;
using std::endl;
using namespace mvme::data_server;

namespace
{

struct EventStorage
{
    // one tree per event
    TTree *tree = nullptr;

    // one buffer per datasource in the event
    std::vector<std::vector<float>> buffers;
    uint32_t hits = 0u;
};

class Context: public mvme::data_server::Parser
{
    public:
        bool doQuit() const { return m_quit; }
        void setConvertNaNsToZero(bool doConvert) { m_convertNaNs = doConvert; }

    protected:
        virtual void serverInfo(const Message &msg, const json &info) override;
        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) override;

        virtual void eventData(const Message &msg, int eventIndex,
                                const std::vector<DataSourceContents> &contents) override;

        virtual void endRun(const Message &msg) override;

        virtual void error(const Message &msg, const std::exception &e) override;

    private:
        std::unique_ptr<TFile> m_outFile;
        std::vector<EventStorage> m_trees;
        bool m_quit = false;
        bool m_convertNaNs = false;
};

void Context::serverInfo(const Message &msg, const json &info)
{
    cout << __FUNCTION__ << ": serverInfo=" << endl << info.dump(2) << endl;
}

void Context::beginRun(const Message &msg, const StreamInfo &streamInfo)
{
    cout << __FUNCTION__ << ": runId=" << streamInfo.runId
        << endl;

    std::string filename;

    if (streamInfo.runId.empty())
        filename = "unknown_run.root";
    else
        filename = streamInfo.runId + ".root";

    m_trees.clear();
    m_outFile = std::make_unique<TFile>(filename.c_str(), "RECREATE",
                                        streamInfo.runId.c_str());

    for (const EventDataDescription &edd: streamInfo.eventDescriptions)
    {
        const VMEEvent &event = streamInfo.vmeTree.events[edd.eventIndex];

        EventStorage storage;

        storage.tree = new TTree(event.name.c_str(),  // name
                                 event.name.c_str()); // title

        cout << "event#" << edd.eventIndex << ", " << event.name << endl;

        for (const DataSourceDescription &dsd: edd.dataSources)
        {
            storage.buffers.emplace_back(std::vector<float>(dsd.size));

            // branchSpec looks like: "name[Size]/D" for doubles,
            // "name[Size]/F" for floats
            std::ostringstream ss;
            ss << dsd.name.c_str() << "[" << dsd.size << "]/F";
            std::string branchSpec = ss.str();

            cout << "  data branch: " << branchSpec << endl;

            auto branch = storage.tree->Branch(
                dsd.name.c_str(),
                storage.buffers.back().data(),
                branchSpec.c_str());

            assert(!branch->IsZombie());
        }

        m_trees.emplace_back(std::move(storage));
    }

    cout << "Receiving data..." << endl;
}

void Context::eventData(const Message &msg, int eventIndex,
                        const std::vector<DataSourceContents> &contents)
{
    assert(0 <= eventIndex && static_cast<size_t>(eventIndex) < m_trees.size());

    auto &eventStorage = m_trees[eventIndex];

    assert(eventStorage.tree);
    assert(eventStorage.buffers.size() == contents.size());

    for (size_t dsIndex = 0; dsIndex < contents.size(); dsIndex++)
    {
        const DataSourceContents &dsc = contents[dsIndex];

        std::vector<float> &buffer = eventStorage.buffers[dsIndex];

        assert(dsc.dataEnd - dsc.dataBegin == static_cast<ptrdiff_t>(buffer.size()));

        //cout << __PRETTY_FUNCTION__
        //    << " buffer=" << buffer.data() << endl;

        if (m_convertNaNs)
        {
            // Copy, but transform NaN values to 0.0
            std::transform(dsc.dataBegin, dsc.dataEnd, buffer.begin(),
                           [] (const double value) -> float {
                               return std::isnan(value) ? 0.0 : value;
                           });
        }
        else
        {
            // Copy, including conversion from double to float
            std::copy(dsc.dataBegin, dsc.dataEnd, buffer.begin());
        }
    }

    eventStorage.tree->Fill();
    eventStorage.hits++;
}

void Context::endRun(const Message &msg)
{
    cerr << __FUNCTION__ << endl;

    if (m_outFile)
    {
        cout << "Closing output file " << m_outFile->GetName() << "..." << endl;
        m_outFile->Close();
        m_outFile.release();
    }

    cout << "HitCounts by event:" << endl;
    for (size_t ei=0; ei < m_trees.size(); ei++)
    {
        const auto &es = m_trees[ei];

        cout << "  ei=" << ei << ", hits=" << es.hits << endl;
    }

    cout << endl;
}

void Context::error(const Message &msg, const std::exception &e)
{
    cout << "An error occured: " << e.what() << endl;

    if (m_outFile)
    {
        cout << "Closing output file " << m_outFile->GetName() << "..." << endl;
        m_outFile->Close();
        m_outFile.release();
    }

    m_quit = true;
}

static bool signal_received = false;

void signal_handler(int signum)
{
    cout << "signal " << signum << endl;
    cout.flush();
    signal_received = true;
}

void setup_signal_handler()
{
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handler");
    }
}

} // end anon namespace

int main(int argc, char *argv[])
{
    // host, port, quit after one run?,
    // output filename? if not specified is taken from the runId
    // send out a reply is response to the EndRun message?
    std::string host = "localhost";
    std::string port = "13801";

    setup_signal_handler();

    // A single message object, whose buffer is reused for each incoming
    // message.
    Message msg;

    // Subclass of mvme::data_server::Parser implementing the ROOT tree
    // creation. This is driven through handleMessage() which then calls our
    // specialized handlers.
    Context ctx;
    int sockfd = -1;

    while (!ctx.doQuit() && !signal_received)
    {
        if (sockfd < 0)
        {
            cout << "Connecting to " << host << ":" << port << endl;
        }

        while (sockfd < 0 && !signal_received)
        {
            try
            {
                sockfd = connect_to(host.c_str(), port.c_str());
            }
            catch (const connection_error &e)
            {
                sockfd = -1;
            }

            if (sockfd >= 0)
            {
                cout << "Connected to " << host << ":" << port << endl;
                break;
            }

            if (usleep(1000 * 1000) != 0 && errno != EINTR)
            {
                throw std::system_error(errno, std::generic_category(), "usleep");
            }
        }

        if (signal_received) break;

        try
        {
            read_message(sockfd, msg);
            ctx.handleMessage(msg);
        }
        catch (const connection_error &e)
        {
            cout << "Disconnected from " << host << ":" << port << endl;
            sockfd = -1;
            ctx.reset();
        }
    }

    return 0;
}
