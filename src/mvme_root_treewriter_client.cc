#include "mvme_data_server_lib.h"

#include <getopt.h>
#include <regex>
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

std::string make_branch_name(const std::string &input)
{
    std::regex re("/|\\|[|]|\\.");
    return std::regex_replace(input, re, "_");
}

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
        void setQuitAfterRun(bool b) { m_quitAfterRun = b; }

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
        bool m_quitAfterRun = false;
};

void Context::serverInfo(const Message &msg, const json &info)
{
    cout << __FUNCTION__ << ": serverInfo:" << endl << info.dump(2) << endl;
}

void Context::beginRun(const Message &msg, const StreamInfo &streamInfo)
{
    cout << __FUNCTION__ << ": run information:"
        << endl << streamInfo.infoJson.dump(2) << endl;

    cout << __FUNCTION__ << ": runId=" << streamInfo.runId
        << endl;

    std::string filename;

    if (streamInfo.runId.empty())
    {
        cout << __FUNCTION__ << ": Warning: got an empty runId!" << endl;
        filename = "unknown_run.root";
    }
    else
    {
        filename = streamInfo.runId + ".root";
    }

    cout << __FUNCTION__ << ": Using output filename '" << filename << "'" << endl;

    m_trees.clear();
    m_outFile = std::make_unique<TFile>(filename.c_str(), "RECREATE",
                                        streamInfo.runId.c_str());

    // For each incoming event: create a TTree, buffer space and branches
    for (const EventDataDescription &edd: streamInfo.eventDescriptions)
    {
        const VMEEvent &event = streamInfo.vmeTree.events[edd.eventIndex];

        EventStorage storage;

        storage.tree = new TTree(event.name.c_str(),  // name
                                 event.name.c_str()); // title

        cout << "  event#" << edd.eventIndex << ", " << event.name << endl;

        for (const DataSourceDescription &dsd: edd.dataSources)
        {
            // The branch will point to this buffer space
            storage.buffers.emplace_back(std::vector<float>(dsd.size));

            // branchSpec looks like: "name[Size]/D" for doubles,
            // "name[Size]/F" for floats
            std::ostringstream ss;
            ss << make_branch_name(dsd.name) << "[" << dsd.size << "]/F";
            std::string branchSpec = ss.str();

            cout << "    data branch: " << dsd.name << " -> " << branchSpec << endl;

            auto branch = storage.tree->Branch(
                dsd.name.c_str(),
                storage.buffers.back().data(),
                branchSpec.c_str());

            assert(!branch->IsZombie());
        }

        m_trees.emplace_back(std::move(storage));
    }

    cout << "Created output tree structures" << endl;
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
            // Copy data, implicitly converting incoming doubles to float
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
        cout << "  Closing output file " << m_outFile->GetName() << "..." << endl;
        m_outFile->Write();
        m_outFile->Close();
        m_outFile.release();
    }

    cout << "  HitCounts by event:" << endl;
    for (size_t ei=0; ei < m_trees.size(); ei++)
    {
        const auto &es = m_trees[ei];

        cout << "    ei=" << ei << ", hits=" << es.hits << endl;
    }

    cout << endl;

    if (m_quitAfterRun) m_quit = true;
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

void setup_signal_handlers()
{
    /* Set up the structure to specify the new action. */
    struct sigaction new_action;
    new_action.sa_handler = signal_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    for (auto signum: { SIGINT, SIGHUP, SIGTERM })
    {
        if (sigaction(signum, &new_action, NULL) != 0)
            throw std::system_error(errno, std::generic_category(), "setup_signal_handlers");
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
    bool singleRun = false;
    bool convertNaNs = false;
    bool showHelp = false;

    while (true)
    {
        static struct option long_options[] =
        {
            { "single-run",             no_argument, nullptr,    0 },
            { "convert-nans",           no_argument, nullptr,    0 },
            { "help",                   no_argument, nullptr,    0 },
            { nullptr, 0, nullptr, 0 },
        };

        int option_index = 0;
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        if (c == '?') // Unrecognized option
        {
            return 1;
        }

        if (c != 0)
            break;

        std::string opt_name(long_options[option_index].name);

        if (opt_name == "single-run")   singleRun = true;
        if (opt_name == "convert-nans") convertNaNs = true;
        if (opt_name == "help")         showHelp = true;
    }

    if (showHelp)
    {
        cout << "Usage: " << argv[0]
            << " [--single-run] [--convert-nans] [host=localhost] [port=13801]"
            << endl << endl
            ;

        cout << "  If single-run is set the process will exit after receiving" << endl
             << "  data from one run. Otherwise it will wait for the next run to" << endl
             << "  start." << endl << endl

             << "  If convert-nans is set incoming NaN data values will be" << endl
             << "  converted to 0.0 before they are written to their ROOT TBranch." << endl
             << endl
             ;

        return 0;
    }

    if (optind < argc) { host = argv[optind++]; }
    if (optind < argc) { port = argv[optind++]; }

    setup_signal_handlers();

    // Subclass of mvme::data_server::Parser implementing the ROOT tree
    // creation. This is driven through handleMessage() which then calls our
    // specialized handlers.
    Context ctx;
    ctx.setConvertNaNsToZero(convertNaNs);
    ctx.setQuitAfterRun(singleRun);

    // A single message object, whose buffer is reused for each incoming
    // message.
    Message msg;
    int sockfd = -1;
    int retval = 0;

    while (!ctx.doQuit() && !signal_received)
    {
        if (sockfd < 0)
        {
            cout << "Connecting to " << host << ":" << port << " ..." << endl;
        }

        while (sockfd < 0 && !signal_received)
        {
            try
            {
                sockfd = connect_to(host.c_str(), port.c_str());
            }
            catch (const mvme::data_server::exception &e)
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
        catch (const mvme::data_server::connection_closed &)
        {
            cout << "Error: The remote host closed the connection." << endl;
            sockfd = -1;
            // Reset context state as we're going to attempt to reconnect.
            ctx.reset();
        }
        catch (const mvme::data_server::exception &e)
        {
            cout << "An error occured: " << e.what() << endl;
            retval = 1;
            break;
        }
        catch (const std::system_error &e)
        {
            cout << "Disconnected from " << host << ":" << port
                << ", reason: " << e.what() << endl;
            sockfd = -1;
            retval = 1;
            break;
        }
    }

    return retval;
}
