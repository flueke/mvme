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
    //TTree *tree = nullptr;
#if 0
    std::vector<std::vector<double>> dataSourceBuffers;
#else
    std::vector<TNtupleD *> dataSourceTuples;
#endif
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
        std::vector<std::unique_ptr<EventStorage>> m_trees;
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

        auto storage = std::make_unique<EventStorage>();

        //storage->tree = new TTree(event.name.c_str(),  // name
        //                              event.name.c_str()); // title

        //storage.dataSourceBuffers.resize(edd.dataSources.size());

        for (const DataSourceDescription &dsd: edd.dataSources)
        {
#if 0
            storage->dataSourceBuffers.emplace_back(
                std::vector<double>(dsd.size));
#else
            std::ostringstream ss;
            for (uint32_t var = 0; var < dsd.size; var++)
            {
                ss << std::to_string(var);
                if (var < dsd.size - 1) ss << ":";
            }
            std::string varlist = ss.str();
            cout << varlist << endl;

            storage->dataSourceTuples.emplace_back(
                new TNtupleD(
                    dsd.name.c_str(),
                    dsd.name.c_str(),
                    varlist.c_str()));

#endif

#if 0 // name[N]/D
            cout << __PRETTY_FUNCTION__
                << " buffer=" << storage->dataSourceBuffers.back().data() << endl;
            std::ostringstream ss;
            ss << dsd.name.c_str() << "[" << dsd.size << "]/D";
            std::string branchSpec = ss.str();

            cout << branchSpec << endl;

            auto branch = storage->tree->Branch(
                dsd.name.c_str(),
                storage->dataSourceBuffers.back().data(),
                branchSpec.c_str());
#elif 0 // STLCollection
            auto branch = storage->tree->Branch(
                dsd.name.c_str(),
                &storage->dataSourceBuffers.back());
#elif 1
#endif

            assert(!branch->IsZombie());
        }

        m_trees.emplace_back(std::move(storage));
    }
}

void Context::eventData(const Message &msg, int eventIndex,
                        const std::vector<DataSourceContents> &contents)
{
    assert(0 <= eventIndex && eventIndex < m_trees.size());

    auto &eventStorage = m_trees[eventIndex];

    //assert(eventStorage->tree);
    assert(eventStorage->dataSourceBuffers.size() == contents.size());

    for (size_t dsIndex = 0; dsIndex < contents.size(); dsIndex++)
    {
        const auto &dsc = contents[dsIndex];
        
#if 0
        std::vector<double> &buffer = eventStorage->dataSourceBuffers[dsIndex];

        //cout << __PRETTY_FUNCTION__
        //    << " buffer=" << buffer.data() << endl;

        std::copy(dsc.dataBegin, dsc.dataEnd, buffer.begin());
        //std::cerr << buffer.front() << endl;
#else
        auto ntuple = eventStorage->dataSourceTuples[dsIndex];
        ntuple->Fill(dsc.dataBegin);
#endif
    }

    //eventStorage->tree->Fill();
}

void Context::endRun(const Message &msg)
{
    cerr << __PRETTY_FUNCTION__ << endl;

    if (m_outFile)
        m_outFile->Close();

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
