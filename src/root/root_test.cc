// https://root.cern.ch/root/htmldoc/guides/users-guide/ROOTUsersGuide.html#the-physical-layout-of-root-files
// https://root.cern.ch/root/htmldoc/guides/users-guide/ROOTUsersGuide.html#tasks

#include <TFile.h>

int main(int argc, char *argv[])
{
    TFile f("test.root", "recreate");

    f.Close();

    return 0;
}
