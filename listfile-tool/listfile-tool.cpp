#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

using std::cout;
using std::cerr;
using std::endl;

namespace listfile
{
    /* Section types in the listfile. */
    enum SectionType
    {
        /* The config section contains the mvmecfg as a json string padded with
         * spaces to the next 32 bit boundary. If the config data size exceeds
         * the maximum section size multiple config sections will be written at
         * the start of the file. */
        SectionType_Config = 0,
        /* Section containing event data. */
        SectionType_Event  = 1,
        /* End section should occur once at the end of the file. Can be used to
         * verify that the file was closed properly when writing. Otherwise unused.
         * Contains no data. */
        SectionType_End    = 2,

        SectionType_Max    = 7
    };

    static const int SectionMaxWords  = 0xffff;
    static const int SectionMaxSize   = SectionMaxWords * sizeof(u32);

    static const int SectionTypeMask  = 0xe0000000; // 3 bit section type
    static const int SectionTypeShift = 29;
    static const int SectionSizeMask  = 0xffff;    // 16 bit section size in 32 bit words
    static const int SectionSizeShift = 0;
    static const int EventTypeMask  = 0xf0000;   // 4 bit event type
    static const int EventTypeShift = 16;

    // Subevent containing module data
    static const int ModuleTypeMask  = 0x3f000; // 6 bit module type
    static const int ModuleTypeShift = 12;

    static const int SubEventMaxWords  = 0x3ff;
    static const int SubEventMaxSize   = SubEventMaxWords * sizeof(u32);
    static const int SubEventSizeMask  = 0x3ff; // 10 bit subevent size in 32 bit words
    static const int SubEventSizeShift = 0;

    enum VMEModuleType
    {
        Invalid = 0,
        MADC32  = 1,
        MQDC32  = 2,
        MTDC32  = 3,
        MDPP16  = 4,
        MDPP32  = 5,
        MDI2    = 6,

        MesytecCounter = 16,
        VHS4030p = 21,
    };

    static const std::map<VMEModuleType, const char *> VMEModuleTypeNames =
    {
        { VMEModuleType::MADC32,            "MADC-32" },
        { VMEModuleType::MQDC32,            "MQDC-32" },
        { VMEModuleType::MTDC32,            "MTDC-32" },
        { VMEModuleType::MDPP16,            "MDPP-16" },
        { VMEModuleType::MDPP32,            "MDPP-32" },
        { VMEModuleType::MDI2,              "MDI-2" },
        { VMEModuleType::VHS4030p,          "iseg VHS4030p" },
        { VMEModuleType::MesytecCounter,    "Mesytec Counter" },
    };

    const char *get_vme_module_name(VMEModuleType moduleType)
    {
        auto it = VMEModuleTypeNames.find(moduleType);
        if (it != VMEModuleTypeNames.end())
        {
            return it->second;
        }

        return "unknown";
    }

} // end namespace listfile

void process_listfile(std::ifstream &infile);

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        cerr << "Invalid number of arguments" << endl;
        cerr << "Usage: " << argv[0] << " <listfile>" << endl;
        return 1;
    }

    std::ifstream infile(argv[1], std::ios::binary);

    if (!infile.is_open())
    {
        cerr << "Error opening " << argv[1] << " for reading: " << std::strerror(errno) << endl;
        return 1;
    }

    infile.exceptions(std::ifstream::badbit | std::ifstream::failbit | std::ifstream::eofbit);

    try
    {
        process_listfile(infile);
    }
    catch (const std::exception &e)
    {
        cerr << "Error processing listfile: " << e.what() << endl;
        return 1;
    }

    return 0;
}

void process_listfile(std::ifstream &infile)
{
    using namespace listfile;

    bool dumpData = true;
    bool continueReading = true;

    while (continueReading)
    {
        u32 sectionHeader;
        infile.read((char *)&sectionHeader, sizeof(u32));

        u32 sectionType   = (sectionHeader & SectionTypeMask) >> SectionTypeShift;
        u32 sectionSize   = (sectionHeader & SectionSizeMask) >> SectionSizeShift;

        switch (sectionType)
        {
            case SectionType_Config:
                {
                    cout << "Config section of size " << sectionSize << endl;
                    infile.seekg(sectionSize * sizeof(u32), std::ifstream::cur);
                } break;

            case SectionType_Event:
                {
                    u32 eventType = (sectionHeader & EventTypeMask) >> EventTypeShift;
                    printf("Event section: eventHeader=0x%08x, eventType=%d, eventSize=%u\n",
                           sectionHeader, eventType, sectionSize);

                    u32 wordsLeft = sectionSize;

                    while (wordsLeft > 1)
                    {
                        u32 subEventHeader;
                        infile.read((char *)&subEventHeader, sizeof(u32));
                        --wordsLeft;

                        u32 moduleType = (subEventHeader & ModuleTypeMask) >> ModuleTypeShift;
                        u32 subEventSize = (subEventHeader & SubEventSizeMask) >> SubEventSizeShift;

                        printf("  subEventHeader=0x%08x, moduleType=%u (%s), subEventSize=%u\n",
                               subEventHeader, moduleType, get_vme_module_name((VMEModuleType)moduleType),
                               subEventSize);

                        for (u32 i=0; i<subEventSize; ++i)
                        {
                            u32 subEventData;
                            infile.read((char *)&subEventData, sizeof(u32));

                            if (dumpData)
                                printf("    %u = 0x%08x\n", i, subEventData);
                        }
                        wordsLeft -= subEventSize;
                    }

                    u32 eventEndMarker;
                    infile.read((char *)&eventEndMarker, sizeof(u32));
                    printf("   eventEndMarker=0x%08x\n", eventEndMarker);
                } break;

            case SectionType_End:
                {
                    printf("Found Listfile End section\n");
                    continueReading = false;
                    break;
                }

            default:
                {
                    printf("Warning: Unknown section type %u of size %u, skipping...\n",
                           sectionType, sectionSize);
                    infile.seekg(sectionSize * sizeof(u32), std::ifstream::cur);
                } break;
        }
    }
}
