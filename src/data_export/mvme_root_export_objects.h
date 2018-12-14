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

        size_t GetNumberOfSubevents() const { return fSubevents.size(); }
        std::vector<Module *> GetSubevents() const { return fSubevents; }

    protected:
        void AddSubevent(Module *subevent)
        {
            fSubevents.push_back(subevent);
        }

    private:
        std::vector<Module *> fSubevents; // !

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
