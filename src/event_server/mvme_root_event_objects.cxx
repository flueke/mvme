#include "event_server/mvme_root_event_objects.h"
// This include is only here to have cmake create a dependency on the linkdef
// header file.
#include "event_server/mvme_root_event_objects_LinkDef.h"

#include <cassert>

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
        }
    }

    return result;
}
