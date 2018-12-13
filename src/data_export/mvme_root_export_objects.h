#ifndef __MVME_ROOT_EXPORT_OBJECTS_H__
#define __MVME_ROOT_EXPORT_OBJECTS_H__

#include <TNamed.h>
#include <TTree.h>

//
// Event and Subevent base classes
//
class Subevent: public TNamed
{
    public:
        Subevent(const char *name, const char *title)
            : TNamed(name, title)
        {}

    ClassDef(Subevent, 1);
};

class Event: public TNamed
{
    public:
        Event(const char *name, const char *title)
            : TNamed(name, title)
        {}

        size_t GetNumberOfSubevents() const { return fSubevents.size(); }
        std::vector<Subevent *> GetSubevents() const { return fSubevents; }

    protected:
        void AddSubevent(Subevent *subevent)
        {
            fSubevents.push_back(subevent);
        }

    private:
        std::vector<Subevent *> fSubevents; // !

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

        // The only reason this is a virtual method is for hotloading of the
        // generated code via TROOT::ProcessLine()
        virtual std::vector<TTree *> MakeTrees();

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
