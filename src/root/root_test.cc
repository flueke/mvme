// https://root.cern.ch/root/htmldoc/guides/users-guide/ROOTUsersGuide.html#the-physical-layout-of-root-files
// https://root.cern.ch/root/htmldoc/guides/users-guide/ROOTUsersGuide.html#tasks

#include <TFile.h>
#include <TRandom.h>
#include <TTree.h>
#include "mvme_root.h"

#define ArrayCount(x) (sizeof(x) / sizeof(*x))

#if 1
int main(int argc, char *argv[])
{
    TRandom rng;
    TFile f("test.root", "recreate");

    u32 testData[] = { 0xbb000000, 1, 2, 3, 4, 5, 6, 7, 8, 0xee000000 };

    TTree myTree("MyTree", "this is my tree");

    ModuleEvent e;
    e.eventIndex = 0;
    e.moduleIndex = 0;
    e.data.reserve(ArrayCount(testData));
    std::copy(testData, testData + ArrayCount(testData), std::back_inserter(e.data));

    myTree.Branch("ModuleBranch", "ModuleEvent", &e);

    for (s32 i = 0; i < 10000; i++)
    {
        myTree.Fill();
    }

    myTree.Write();

    f.Close();

    return 0;
}
#endif

#if 0
struct ModuleEventNoObject
{
    u16 eventIndex;
    u16 moduleIndex;
    u32 size = 0;
    u32 *data = nullptr;
};

int main(int argc, char *argv[])
{
    TFile f("test.root", "recreate");

    u32 testData[] = { 0xbb000000, 1, 2, 3, 4, 5, 6, 7, 8, 0xee000000 };

    TTree myTree("MyTree", "this is my tree");

    ModuleEventNoObject e;
    e.eventIndex = 0;
    e.moduleIndex = 0;
    e.size = ArrayCount(testData);
    e.data = testData;

    myTree.Branch("sizes", &e.size, "size/i");
    myTree.Branch("data", e.data, "data[size]/i");

    for (s32 i = 0; i < 100; i++)
    {
        myTree.Fill();
    }

    myTree.Write();

    f.Close();

    return 0;
}
#endif

#if 0
int main(int argc, char *argv[])
{
    TFile f("test.root", "recreate");

    u32 testData[] = { 0xbb000000, 1, 2, 3, 4, 5, 6, 7, 8, 0xee000000 };

    ModuleEvent moduleEvent;
    moduleEvent.eventIndex = 0;
    moduleEvent.moduleIndex = 0;
    moduleEvent.size = ArrayCount(testData);
    moduleEvent.data = testData;

    moduleEvent.Write();

    f.Close();

    return 0;
}
#endif
