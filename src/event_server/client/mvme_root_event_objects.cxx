#include "mvme_root_event_objects.h"

#include <cassert>

//ClassImp(MVMEModule)
MVMEModule::MVMEModule(const char *name, const char *title)
    : TNamed(name, title)
{}

//MVMEModule::~MVMEModule()
//{}

void MVMEModule::RegisterDataStorage(double *ptr, size_t size)
{
    fDataStores.push_back({ptr, size});
}

//ClassImp(MVMEEvent)
MVMEEvent::MVMEEvent(const char *name, const char *title)
    : TNamed(name, title)
{}

void MVMEEvent::AddModule(MVMEModule *module)
{
    fModules.push_back(module);

    for (auto storage: module->GetDataStorages())
    {
        // Same order as incoming data during the run (and as declared in the
        // BeginRun description data)
        fDataSourceStorages.push_back(storage);
    }
}

Storage MVMEEvent::GetDataSourceStorage(int dsIndex) const
{
    if (0 <= dsIndex && dsIndex < static_cast<int>(fDataSourceStorages.size()))
        return fDataSourceStorages.at(dsIndex);
    return {};
}

//ClassImp(Experiment)
Experiment::Experiment(const char *name, const char *title)
    : TNamed(name, title)
{}

void Experiment::AddEvent(MVMEEvent *event)
{
    fEvents.push_back(event);
}

std::vector<TTree *> Experiment::MakeTrees()
{
    std::vector<TTree *> result;

    for (auto event: GetEvents())
    {
        auto tree = new TTree(event->GetName(), Form("Tree for event %s", event->GetTitle()));
        result.push_back(tree);

        for (auto module: event->GetModules())
        {
            auto branch = tree->Branch(module->GetName(), module);
            assert(!branch->IsZombie());
            (void)branch;
        }
    }

    return result;
}

std::vector<TTree *> Experiment::InitTrees(TFile *inputFile)
{
    std::vector<TTree *> result;

    for (auto event: GetEvents())
    {
        auto tree = dynamic_cast<TTree *>(inputFile->Get(event->GetName()));
        if (tree)
        {
            for (auto module: event->GetModules())
            {
                auto branch = tree->GetBranch(module->GetName());
                if (branch)
                    branch->SetAddress(module);
            }
        }
        result.push_back(tree);
    }

    return result;
}

MVMEEvent *Experiment::GetEvent(int eventIndex) const
{
    if (0 <= eventIndex && eventIndex < static_cast<int>(fEvents.size()))
        return fEvents.at(eventIndex);
    return nullptr;
}
