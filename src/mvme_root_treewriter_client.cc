#include "data_server_client_lib.h"

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

    protected:
        virtual void beginRun(const Message &msg, const StreamInfo &streamInfo) override;

        virtual void eventData(const Message &msg, int eventIndex,
                                const std::vector<DataSourceContents> &contents) override;

        virtual void endRun(const Message &msg) override;

        virtual void error(const Message &msg, const std::exception &e) override;

    private:
        std::unique_ptr<TFile> m_outFile;
        std::vector<EventStorage> m_trees;
        bool m_quit = false;
};

    // TODO: vector index checks

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
        VMEEvent event = streamInfo.vmeTree.events[edd.eventIndex];

        EventStorage storage;

        storage.tree = new TTree(event.name.c_str(),  // name
                                 event.name.c_str()); // title

        for (const DataSourceDescription &dsd: edd.dataSources)
        {
            storage.buffers.emplace_back(std::vector<float>(dsd.size));

            // branchSpec looks like: "name[Size]/D" for doubles
            // branchSpec looks like: "name[Size]/F" for floats
            cout << __PRETTY_FUNCTION__
                << " buffer=" << storage.buffers.back().data() << endl;
            std::ostringstream ss;
            ss << dsd.name.c_str() << "[" << dsd.size << "]/F";
            std::string branchSpec = ss.str();

            cout << branchSpec << endl;

            auto branch = storage.tree->Branch(
                dsd.name.c_str(),
                storage.buffers.back().data(),
                branchSpec.c_str());

            assert(!branch->IsZombie());
        }

        m_trees.emplace_back(std::move(storage));
    }
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
        const auto &dsc = contents[dsIndex];

        std::vector<float> &buffer = eventStorage.buffers[dsIndex];

        //cout << __PRETTY_FUNCTION__
        //    << " buffer=" << buffer.data() << endl;

        // Copy, including conversion from double to float
        //std::copy(dsc.dataBegin, dsc.dataEnd, buffer.begin());

        // Copy, but transform NaN values to 0.0
        std::transform(dsc.dataBegin, dsc.dataEnd, buffer.begin(),
                       [] (const double value) -> float {
                           return std::isnan(value) ? 0.0 : value;
                       });
    }

    eventStorage.tree->Fill();
    eventStorage.hits++;
}

void Context::endRun(const Message &msg)
{
    cerr << __PRETTY_FUNCTION__ << endl;

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

    m_quit = true;
}

void Context::error(const Message &msg, const std::exception &e)
{
}

} // end anon namespace

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
