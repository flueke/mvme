/* listfile-multievent-dumper - Reconstruct events from a multievent readout listfile
 *
 * Copyright (C) 2017  Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include "databuffer.h"
#include "globals.h"
#include "data_filter.h"

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

/*  ===== VERSION 0 =====
 *
 *  ------- Section (Event) Header ----------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |ttt         eeeessssssssssssssss|
 * +--------------------------------+
 *
 * t =  3 bit section type
 * e =  4 bit event type (== event number/index) for event sections
 * s = 16 bit size in units of 32 bit words (fillwords added to data if needed) -> 256k section max size
 *
 * Section size is the number of following 32 bit words not including the header word itself.

 * Sections with SectionType_Event contain subevents with the following header:

 *  ------- Subevent (Module) Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |              mmmmmm  ssssssssss|
 * +--------------------------------+
 *
 * m =  6 bit module type (VMEModuleType enum from globals.h)
 * s = 10 bit size in units of 32 bit words
 *
 * The last word of each event section is the EndMarker (globals.h)
 *
*/
struct listfile_v0
{
    static const int Version = 0;
    static const int FirstSectionOffset = 0;

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
};

/*  ===== VERSION 1 =====
 *
 * Differences to version 0:
 * - Starts with the FourCC "MVME" followed by a 32 bit word containing the
 *   listfile version number.
 * - Larger section and subevent sizes: 16 -> 20 bits for sections and 10 -> 20
 *   bits for subevents.
 * - Module type is now 8 bit instead of 6.
 *
 *  ------- Section (Event) Header ----------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |ttteeee     ssssssssssssssssssss|
 * +--------------------------------+
 *
 * t =  3 bit section type
 * e =  4 bit event type (== event number/index) for event sections
 * s = 20 bit size in units of 32 bit words (fillwords added to data if needed) -> 256k section max size
 *
 * Section size is the number of following 32 bit words not including the header word itself.

 * Sections with SectionType_Event contain subevents with the following header:

 *  ------- Subevent (Module) Header --------
 *  33222222222211111111110000000000
 *  10987654321098765432109876543210
 * +--------------------------------+
 * |mmmmmmmm    ssssssssssssssssssss|
 * +--------------------------------+
 *
 * m =  8 bit module type (VMEModuleType enum from globals.h)
 * s = 10 bit size in units of 32 bit words
 *
 * The last word of each event section is the EndMarker (globals.h)
 *
*/
struct listfile_v1
{
    static const int Version = 1;
    constexpr static const char * const FourCC = "MVME";

    static const int FirstSectionOffset = 8;

    static const int SectionMaxWords  = 0xfffff;
    static const int SectionMaxSize   = SectionMaxWords * sizeof(u32);

    static const int SectionTypeMask  = 0xe0000000; // 3 bit section type
    static const int SectionTypeShift = 29;
    static const int SectionSizeMask  = 0x000fffff; // 20 bit section size in 32 bit words
    static const int SectionSizeShift = 0;
    static const int EventTypeMask    = 0x1e000000; // 4 bit event type
    static const int EventTypeShift   = 25;

    // Subevent containing module data
    static const int ModuleTypeMask  = 0xff000000;  // 8 bit module type
    static const int ModuleTypeShift = 24;

    static const int SubEventMaxWords  = 0xfffff;
    static const int SubEventMaxSize   = SubEventMaxWords * sizeof(u32);
    static const int SubEventSizeMask  = 0x000fffff; // 20 bit subevent size in 32 bit words
    static const int SubEventSizeShift = 0;
};

namespace listfile
{
    enum SectionType
    {
        /* The config section contains the mvmecfg as a json string padded with
         * spaces to the next 32 bit boundary. If the config data size exceeds
         * the maximum section size multiple config sections will be written at
         * the start of the file. */
        SectionType_Config      = 0,

        /* Readout data generated by one VME Event. Contains Subevent Headers
         * to split into VME Module data. */
        SectionType_Event       = 1,

        /* Last section written to a listfile before closing the file. Used for
         * verification purposes. */
        SectionType_End         = 2,

        /* Marker section written once at the start of a run and then once per
         * elapsed second. */
        SectionType_Timetick    = 3,

        /* Max section type possible. */
        SectionType_Max         = 7
    };

    enum VMEModuleType
    {
        Invalid         = 0,
        MADC32          = 1,
        MQDC32          = 2,
        MTDC32          = 3,
        MDPP16_SCP      = 4,
        MDPP32          = 5,
        MDI2            = 6,
        MDPP16_RCP      = 7,
        MDPP16_QDC      = 8,

        MesytecCounter = 16,
        VHS4030p = 21,
    };

    static const std::map<VMEModuleType, const char *> VMEModuleTypeNames =
    {
        { VMEModuleType::MADC32,            "MADC-32" },
        { VMEModuleType::MQDC32,            "MQDC-32" },
        { VMEModuleType::MTDC32,            "MTDC-32" },
        { VMEModuleType::MDPP16_SCP,        "MDPP-16_SCP" },
        { VMEModuleType::MDPP32,            "MDPP-32" },
        { VMEModuleType::MDI2,              "MDI-2" },
        { VMEModuleType::MDPP16_RCP,        "MDPP-16_RCP" },
        { VMEModuleType::MDPP16_QDC,        "MDPP-16_QDC" },
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

static const std::array<DataFilter, 256> ModuleHeaderFiltersByModuleType =
{
    DataFilter(),                                       // 0 -> invalid
    DataFilter(),                                       // 1 -> madc32
    DataFilter(),                                       // 2 -> mqdc32
    DataFilter("01000000MMMMMMMMXXXXSSSSSSSSSSSS"),     // 3 -> mtdc32
    DataFilter("0100XXXXMMMMMMMMXXXXXXSSSSSSSSSS"),     // 4 -> mdpp16_scp
    DataFilter(),                                       // 5 -> invalid
    DataFilter(),                                       // 6 -> mdi2
    DataFilter(),                                       // 7 -> mdpp16_rcp
    DataFilter(),                                       // 8 -> mdpp16_qdc
};

template<typename LF>
void process_listfile(std::ifstream &infile)
{
    using namespace listfile;

    bool dumpData = true;
    bool continueReading = true;
    DataBuffer eventBuffer(Megabytes(1));
    static const u32 MaxModulesPerEvent = 20;
    struct MultiEventModuleInfo
    {
        u32 *subEventHeader = nullptr;  // Points to the mvme header preceeding the module data.
                                        // Null if the entry is not used.
        u32 *moduleHeader   = nullptr;  // Points to the current module data header.
                                        // Null if no header has been read yet.
    };
    std::array<MultiEventModuleInfo, MaxModulesPerEvent> moduleInfos;

    while (continueReading)
    {
        u32 sectionHeader;
        infile.read((char *)&sectionHeader, sizeof(u32));

        u32 sectionType   = (sectionHeader & LF::SectionTypeMask) >> LF::SectionTypeShift;
        u32 sectionSize   = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;

        switch (sectionType)
        {
            case SectionType_Config:
                {
                    cout << "Config section of size " << sectionSize << endl;
                    infile.seekg(sectionSize * sizeof(u32), std::ifstream::cur);
                } break;

            case SectionType_Event:
                {
                    ssize_t bytesToRead = sectionSize * sizeof(u32);
                    eventBuffer.used = 0;
                    eventBuffer.ensureCapacity(bytesToRead);

                    infile.read(reinterpret_cast<char *>(eventBuffer.data), bytesToRead);
                    auto bytesRead = infile.gcount();
                    if (bytesRead != bytesToRead)
                    {
                        throw std::runtime_error("Error reading full section");
                    }
                    eventBuffer.used = bytesRead;
                    printf("Read %ld bytes into event buffer\n", bytesRead);

                    BufferIterator eventIter(eventBuffer.data, eventBuffer.used);

                    //
                    // Step1: collect all subevent headers
                    //
                    moduleInfos.fill({}); // clear state
                    for (u32 moduleIndex = 0; moduleIndex < MaxModulesPerEvent; ++moduleIndex)
                    {
                        if (eventIter.atEnd() || eventIter.peekU32() == EndMarker)
                            break;

                        moduleInfos[moduleIndex].subEventHeader = eventIter.asU32();
                        moduleInfos[moduleIndex].moduleHeader   = eventIter.asU32() + 1; // point to the first module header

                        u32 subEventHeader = eventIter.extractU32();
                        u32 subEventSize   = (subEventHeader & LF::SubEventSizeMask) >> LF::SubEventSizeShift;

                        eventIter.skip(sizeof(u32), subEventSize);
                    }

#if 1
                    cout << "Step 1 complete: " << endl;
                    for (u32 moduleIndex = 0; moduleIndex < MaxModulesPerEvent; ++moduleIndex)
                    {
                        if (!moduleInfos[moduleIndex].subEventHeader)
                            break;

                        u32 moduleType = (*(moduleInfos[moduleIndex].subEventHeader) & LF::ModuleTypeMask) >> LF::ModuleTypeShift;
                        auto &filter = ModuleHeaderFiltersByModuleType[moduleType];

                        printf("  moduleIndex=%d, subEventHeader=0x%08x, filter=%s\n",
                               moduleIndex, *(moduleInfos[moduleIndex].subEventHeader),
                               filter.toString().toLocal8Bit().constData()
                               );

                    }
                    printf("\n");
#endif

                    //
                    // Step2: yield (mod, event) (0, 0), (1, 0), (0, 1), (1, 1), ...
                    // First event from each module, then second event from each module, ...
                    // TODO: make sure this respects buffer size and does not overrun anything
                    // How does this terminate?
                    bool done = false;
                    u32 eventNumber = 0;

                    while (!done)
                    {
                        for (u32 moduleIndex = 0; moduleInfos[moduleIndex].subEventHeader; ++moduleIndex)
                        {
                            auto mi = &moduleInfos[moduleIndex];
                            Q_ASSERT(mi->moduleHeader);

                            u32 moduleType = (*mi->subEventHeader & LF::ModuleTypeMask) >> LF::ModuleTypeShift;
                            auto &filter = ModuleHeaderFiltersByModuleType[moduleType];

                            if (filter.matches(*mi->moduleHeader))
                            {
                                u32 eventSize = filter.extractData(*mi->moduleHeader, 'S');

#if 0
                                printf("Event #%u for moduleIndex=%u, eventSize=%u, moduleType=%u:\n",
                                       eventNumber, moduleIndex, eventSize, moduleType);

                                for (u32 i=0; i<eventSize+1; ++i)
                                {
                                    printf("  %u: 0x%08x\n", i, *(mi->moduleHeader + i));
                                }
#endif

                                mi->moduleHeader = mi->moduleHeader + eventSize + 1;
                            }
                            else
                            {
#if 0
                                printf("Got a non-module header word: 0x%08X\n", *mi->moduleHeader);
#endif
                                done = true;
                                break;
                            }
                        }
                        ++eventNumber;
                    }
                } break;

            case SectionType_Timetick:
                {
                    printf("Timetick\n");
                } break;

            case SectionType_End:
                {
                    printf("Found Listfile End section\n");
                    continueReading = false;

                    auto currentFilePos = infile.tellg();
                    infile.seekg(0, std::ifstream::end);
                    auto endFilePos = infile.tellg();

                    if (currentFilePos != endFilePos)
                    {
                        cout << "Warning: " << (endFilePos - currentFilePos)
                            << " bytes left after Listfile End Section" << endl;
                    }

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

void process_listfile(std::ifstream &infile)
{
    u32 fileVersion = 0;

    // Read the fourCC that's at the start of listfiles from version 1 and up.
    const size_t bytesToRead = 4;
    char fourCC[bytesToRead] = {};

    infile.read(fourCC, bytesToRead);

    if (std::strncmp(fourCC, listfile_v1::FourCC, bytesToRead) == 0)
    {
        infile.read(reinterpret_cast<char *>(&fileVersion), sizeof(fileVersion));
    }

    // Move to the start of the first section
    auto firstSectionOffset = ((fileVersion == 0)
                               ? listfile_v0::FirstSectionOffset
                               : listfile_v1::FirstSectionOffset);

    infile.seekg(firstSectionOffset, std::ifstream::beg);

    cout << "Detected listfile version " << fileVersion << endl;

    if (fileVersion == 0)
    {
        process_listfile<listfile_v0>(infile);
    }
    else
    {
        process_listfile<listfile_v1>(infile);
    }
}

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
