#ifndef __MVME_ROOT_EXPORT_OBJECTS_H__
#define __MVME_ROOT_EXPORT_OBJECTS_H__

#include <TNamed.h>
#include <TTree.h>

//
// Event and Module base classes
//
class Module: public TNamed
{
    public:
        Module(const char *name, const char *title)
            : TNamed(name, title)
        {}

    ClassDef(Module, 1);
};

class Event: public TNamed
{
    public:
        Event(const char *name, const char *title)
            : TNamed(name, title)
        {}

        size_t GetNumberOfModules() const { return fModules.size(); }
        std::vector<Module *> GetModules() const { return fModules; }

    protected:
        void AddModule(Module *module)
        {
            fModules.push_back(module);
        }

    private:
        std::vector<Module *> fModules; // !

    ClassDef(Event, 1);
};

class Experiment: public TNamed
{
    public:
        Experiment(const char *name, const char *title)
            : TNamed(name, title)
        {}

        size_t GetNumberOfEvents() const { return fEvents.size(); }
        std::vector<Event *> GetEvents() const { return fEvents; }

        std::vector<TTree *> MakeTrees();

    protected:
        void AddEvent(Event *event)
        {
            fEvents.push_back(event);
        }

    private:
        std::vector<Event *> fEvents; // !

    ClassDef(Experiment, 1);
};


#endif /* __MVME_ROOT_EXPORT_OBJECTS_H__ */
