#ifndef __MVME_ROOT_EXPORT_OBJECTS_H__
#define __MVME_ROOT_EXPORT_OBJECTS_H__

#include <string>
#include <TFile.h>
#include <TNamed.h>
#include <TTree.h>

struct Storage
{
    double *ptr;
    size_t size;
    unsigned bits;
    std::string name;
    std::vector<std::string> paramNames;
};

//
// MVMEEvent and MVMEModule base classes
//
class MVMEModule: public TNamed
{
    public:
        MVMEModule(const char *name, const char *title);

        std::vector<Storage> &GetDataStorages() { return fDataStores; }
        const std::vector<Storage> &GetDataStorages() const { return fDataStores; }

        void InitBranch(TBranch *branch);

    protected:
        void RegisterDataStorage(
            double *ptr, size_t size, unsigned bits,
            const std::string &name,
            const std::vector<std::string> &paramNames = {});

    private:
        std::vector<Storage> fDataStores; // !
        MVMEModule *fSelf; // ! Same as 'this'. Used for TBranch::SetAddress().

    ClassDef(MVMEModule, 1);
};

class MVMEEvent: public TNamed
{
    public:
        MVMEEvent(const char *name, const char *title);

        size_t GetNumberOfModules() const { return fModules.size(); }
        std::vector<MVMEModule *> GetModules() const { return fModules; }


        size_t GetNumberOfDataSourceStorages() const { return fDataSourceStorages.size(); }
        std::vector<Storage> &GetDataSourceStorages() { return fDataSourceStorages; }
        const std::vector<Storage> &GetDataSourceStorages() const { return fDataSourceStorages; }
        Storage GetDataSourceStorage(int dsIndex) const;

    protected:
        void AddModule(MVMEModule *module);

    private:
        std::vector<MVMEModule *> fModules; // !
        std::vector<Storage> fDataSourceStorages; // !

    ClassDef(MVMEEvent, 1);
};

class MVMEExperiment: public TNamed
{
    public:
        MVMEExperiment(const char *name, const char *title);

        size_t GetNumberOfEvents() const { return fEvents.size(); }
        std::vector<MVMEEvent *> GetEvents() const { return fEvents; }
        MVMEEvent *GetEvent(int eventIndex) const;

        std::vector<TTree *> MakeTrees();
        std::vector<TTree *> InitTrees(TFile *inputFile);

    protected:
        void AddEvent(MVMEEvent *event);

    private:
        std::vector<MVMEEvent *> fEvents; // !

    ClassDef(MVMEExperiment, 1);
};

#endif /* __MVME_ROOT_EXPORT_OBJECTS_H__ */
