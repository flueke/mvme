#ifndef __MVME_ROOT_EXPORT_OBJECTS_H__
#define __MVME_ROOT_EXPORT_OBJECTS_H__

#include <TNamed.h>
#include <TTree.h>

struct Storage
{
    double *ptr;
    size_t size;
};

//
// MVMEEvent and MVMEModule base classes
//
class MVMEModule: public TNamed
{
    public:
        MVMEModule(const char *name, const char *title);

        std::vector<Storage> GetDataStorages() const { return fDataStores; }

    protected:
        void RegisterDataStorage(double *ptr, size_t size);

    private:
        std::vector<Storage> fDataStores; // !

    ClassDef(MVMEModule, 1);
};

class MVMEEvent: public TNamed
{
    public:
        MVMEEvent(const char *name, const char *title);

        size_t GetNumberOfModules() const { return fModules.size(); }
        std::vector<MVMEModule *> GetModules() const { return fModules; }
        std::vector<Storage> GetDataSourceStorages() const { return fDataSourceStorages; }
        Storage GetDataSourceStorage(int dsIndex) const;

    protected:
        void AddModule(MVMEModule *module);

    private:
        std::vector<MVMEModule *> fModules; // !
        std::vector<Storage> fDataSourceStorages; // !

    ClassDef(MVMEEvent, 1);
};

class Experiment: public TNamed
{
    public:
        Experiment(const char *name, const char *title);

        size_t GetNumberOfEvents() const { return fEvents.size(); }
        std::vector<MVMEEvent *> GetEvents() const { return fEvents; }
        MVMEEvent *GetEvent(int eventIndex) const;

        std::vector<TTree *> MakeTrees();

    protected:
        void AddEvent(MVMEEvent *event);

    private:
        std::vector<MVMEEvent *> fEvents; // !

    ClassDef(Experiment, 1);
};


#endif /* __MVME_ROOT_EXPORT_OBJECTS_H__ */
