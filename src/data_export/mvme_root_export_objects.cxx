#include "mvme_root_export_objects.h"

std::vector<TTree *> Experiment::MakeTrees()
{
    std::vector<TTree *> result;

    for (auto event: GetEvents())
    {
        auto tree = new TTree(event->GetName(), Form("Tree for event %s", event->GetTitle()));
        result.push_back(tree);

        for (auto subevent: event->GetSubevents())
        {
            auto branch = tree->Branch(subevent->GetName(), subevent);
        }
    }

    return result;
}
