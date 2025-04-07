//#include "mvme_session.h"
#include <argh.h>
#include <iostream>

extern "C"
{
#include <gsi-mbs-api/fLmd.h>
}

int main(int argc, char *argv[])
{
    argh::parser parser;
    parser.parse(argc, argv);

    if (parser.pos_args().size() != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <lmd-file> <mvme-vme-config-file>" << std::endl;
        return 1;
    }

    auto lmdFilename = parser.pos_args()[1];
    auto mvmeVmeConfigFilename = parser.pos_args()[2];

    sLmdControl lmdControl = {};
    if (auto res = fLmdGetOpen(&lmdControl, const_cast<char *>(lmdFilename.c_str()), nullptr, 0, 0); res != LMD__SUCCESS)
    {
        std::cerr << "Error opening LMD file: " << lmdFilename << std::endl;
        return 1;
    }
    return 0;
}
