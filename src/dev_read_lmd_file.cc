//#include "mvme_session.h"
#include <argh.h>
#include <iostream>

extern "C"
{
//#include <gsi-mbs-api/fLmd.h>
#include <gsi-mbs-api/f_evt.h>
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

    #if 0 // Leads to an invalid file format error due to some magic number not matching.

    sLmdControl lmdControl = {};
    if (auto res = fLmdGetOpen(&lmdControl, const_cast<char *>(lmdFilename.c_str()), nullptr, 0, 0); res != LMD__SUCCESS)
    {
        std::cerr << "Error opening LMD file: " << lmdFilename << std::endl;
        return 1;
    }

    #else

    // open
    void *headPtr = nullptr;
    auto eventChannel = f_evt_control();
    bool doPrint = true; // make the api output file (any maybe additional) info
    auto status = f_evt_get_tagopen(
        eventChannel,
        nullptr, // tag file
        const_cast<char *>(lmdFilename.c_str()), // lmd file
        reinterpret_cast<char **>(&headPtr), // may be null in which case no file "header or other" information is returned
        doPrint
    );
    if (status) throw status;

    // event loop
    // xxx: leftoff here
    //while (true)
    //{
    //    s_ve10_1 *eventPtr = nullptr;
    //    std::int32_t eventsToSkip = 0;
    //    auto status = f_evt_get_tagnext(eventChannel, eventsToSkip, &eventPtr);
    //}

    // close & cleanup
    status = f_evt_get_tagclose(eventChannel);
    if (status) throw status;

    free(eventChannel);

    #endif

    return 0;
}
