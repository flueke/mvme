#include "mvme_root_event_objects.h"

#include <cassert>
#include <iostream>

using std::cout;
using std::endl;

MVMEModule::MVMEModule(const char *name, const char *title)
    : TNamed(name, title)
    , fSelf(this)
{}

void MVMEModule::RegisterDataStorage(double *ptr, size_t size)
{
    fDataStores.push_back({ptr, size});
}

void MVMEModule::InitBranch(TBranch *branch)
{
    cout << __PRETTY_FUNCTION__
        << "this=" << this
        << ", fSelf=" << fSelf
        << ", &fSelf=" << &fSelf
        << ", branchName=" << branch->GetName()
        << endl;

    branch->SetAddress(&fSelf);
}

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

MVMEExperiment::MVMEExperiment(const char *name, const char *title)
    : TNamed(name, title)
{}

void MVMEExperiment::AddEvent(MVMEEvent *event)
{
    fEvents.push_back(event);
}

std::vector<TTree *> MVMEExperiment::MakeTrees()
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

std::vector<TTree *> MVMEExperiment::InitTrees(TFile *inputFile)
{
    std::vector<TTree *> result;

    for (auto event: GetEvents())
    {

        auto tree = dynamic_cast<TTree *>(inputFile->Get(event->GetName()));

        if (tree)
        {
            cout << "Found tree for event " << event->GetName() << endl;
            for (auto module: event->GetModules())
            {
                if (auto branch = tree->GetBranch(module->GetName()))
                {
                    module->InitBranch(branch);
                }
                else
                {
                    cout << "Error: Did not find branch for module "
                        << module->GetName() << endl;
                }
            }
        }
        else
        {
            cout << "Error: Did not find tree for event " << event->GetName() << endl;
        }

        result.push_back(tree);
    }

    return result;
}

MVMEEvent *MVMEExperiment::GetEvent(int eventIndex) const
{
    if (0 <= eventIndex && eventIndex < static_cast<int>(fEvents.size()))
        return fEvents.at(eventIndex);
    return nullptr;
}
