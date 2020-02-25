#include "mvme_root_event_objects.h"

#include <cassert>
#include <iostream>

using std::cout;
using std::endl;

MVMEModule::MVMEModule(const char *name, const char *title)
    : TNamed(name, title)
    , fSelf(this)
{}

void MVMEModule::RegisterDataStorage(
    double *ptr, size_t size, unsigned bits,
    const std::string &name,
    const std::vector<std::string> &paramNames)
{
    fDataStores.emplace_back(Storage({ptr, size, bits, name, paramNames}));
}

void MVMEModule::InitBranch(TBranch *branch)
{
#if 0
    cout << __PRETTY_FUNCTION__
        << "this=" << this
        << ", fSelf=" << fSelf
        << ", &fSelf=" << &fSelf
        << ", branchName=" << branch->GetName()
        << endl;
#endif

    branch->SetAddress(&fSelf);
}

MVMEEvent::MVMEEvent(const char *name, const char *title)
    : TNamed(name, title)
{}

void MVMEEvent::AddModule(MVMEModule *module)
{
    fModules.emplace_back(module);

    for (auto storage: module->GetDataStorages())
    {
        // Same order as incoming data during the run (and as declared in the
        // BeginRun description data)
        fDataSourceStorages.emplace_back(storage);
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
    fEvents.emplace_back(event);
}

std::vector<TTree *> MVMEExperiment::MakeTrees()
{
    std::vector<TTree *> result;

    for (auto event: GetEvents())
    {
        auto tree = new TTree(event->GetName(), Form("Tree for event '%s'", event->GetName()));

        // Disables TTree autosaves. This way objects in the output file won't
        // have a cycle number != 1 and no versions, each with a different
        // cycle number, will be kept. The drawback is that in case of a crash
        // all the data is lost.
        // Another alternative to get rid of the last two cycles in a tree
        // would be to try to manually delete the 2nd to last cycle from the file.
        tree->SetAutoSave(0);

        result.emplace_back(tree);

        for (auto module: event->GetModules())
        {
            // bufferSize defaults to 32000
            // Increasing this size leads to a smaller output file and a slight
            // speed improvement. Larger buffers compress better.
            static const Int_t bufferSize = 32000 * 1024;

            auto branch = tree->Branch(module->GetName(), module, bufferSize);
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
                    cout << "  Found branch for module " << module->GetName() << endl;
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

        result.emplace_back(tree);
    }

    return result;
}

MVMEEvent *MVMEExperiment::GetEvent(int eventIndex) const
{
    if (0 <= eventIndex && eventIndex < static_cast<int>(fEvents.size()))
        return fEvents.at(eventIndex);
    return nullptr;
}
