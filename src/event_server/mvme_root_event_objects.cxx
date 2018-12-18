#include "event_server/mvme_root_event_objects.h"
// This include is only here to have cmake create a dependency on the linkdef
// header file.
#include "event_server/mvme_root_event_objects_LinkDef.h"

#include <cassert>

Module::Module(const char *name, const char *title)
    : TNamed(name, title)
{}

Module::~Module()
{}

Event::Event(const char *name, const char *title)
    : TNamed(name, title)
{}

void Event::AddModule(Module *module)
{
    fModules.push_back(module);
}

Experiment::Experiment(const char *name, const char *title)
    : TNamed(name, title)
{}

void Experiment::AddEvent(Event *event)
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
        }
    }

    return result;
}
