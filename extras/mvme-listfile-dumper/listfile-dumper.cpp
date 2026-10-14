/* mvme-listfile-dumper - format mvme listfile content and print it to stdout
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
#include <string>
#include <vector>

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

        /* Readout data generated by one VME Event. Contains module data
         * subsections. */
        SectionType_Event       = 1,

        /* Last section written to a listfile before closing the file. Contains
         * the current date and time as an ISO 8601 formatted string padded
         * with zeroes to the next 32-bit boundary (same as Timetick sections).
         * Should be used to verify that a file was correctly written and when
         * reading that all data has been read. */
        SectionType_End         = 2,

        /* Marker section written once at the start of a run and then once per
         * elapsed second. Contains the current date and time as an ISO 8601
         * formatted string padded with zeroes to the next 32-bit boundary. */
        SectionType_Timetick    = 3,

        /* Section marking the beginning and end of a user initiated pause.
         * Contains a single data word with value 0 for pause and value 1 for
         * resume. */
        SectionType_Pause       = 4,

        /* Max section type possible. */
        SectionType_Max         = 7
    };

    enum PauseAction
    {
        Pause = 0,
        Resume = 1,
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
        VMMR            = 9,

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
        { VMEModuleType::VMMR,              "VMMR" },
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

static std::string read_string_data(std::ifstream &infile, size_t bytes)
{
    std::vector<char> buffer(bytes);
    infile.read(buffer.data(), buffer.size());
    std::string result(buffer.begin(), buffer.end());
    return result;;
}

template<typename LF>
void process_listfile(std::ifstream &infile)
{
    using namespace listfile;

    bool dumpData = true;
    bool continueReading = true;
    int64_t currentSecondInRun = 0;

    while (continueReading)
    {
        u32 sectionHeader;
        infile.read((char *)&sectionHeader, sizeof(u32));

        u32 sectionType   = (sectionHeader & LF::SectionTypeMask) >> LF::SectionTypeShift;
        u32 sectionSize   = (sectionHeader & LF::SectionSizeMask) >> LF::SectionSizeShift;
        u32 sectionBytes  = sectionSize * sizeof(u32);

        switch (sectionType)
        {
            case SectionType_Config:
                {
                    std::string str = read_string_data(infile, sectionBytes);

                    cout << "Config section of size " << sectionSize
                        << " words, " << sectionBytes << " bytes." << endl
                        << "First 1000 characters:" << endl;
                    cout << str.substr(0, 1000) << endl << "---" << endl;
                } break;

            case SectionType_Event:
                {
                    u32 eventType = (sectionHeader & LF::EventTypeMask) >> LF::EventTypeShift;
                    printf("Event section: eventHeader=0x%08x, eventType=%u, eventSize=%u\n",
                           sectionHeader, eventType, sectionSize);

                    u32 wordsLeft = sectionSize;

                    while (wordsLeft > 1)
                    {
                        u32 subEventHeader;
                        infile.read((char *)&subEventHeader, sizeof(u32));
                        --wordsLeft;

                        u32 moduleType = (subEventHeader & LF::ModuleTypeMask) >> LF::ModuleTypeShift;
                        u32 subEventSize = (subEventHeader & LF::SubEventSizeMask) >> LF::SubEventSizeShift;

                        printf("  subEventHeader=0x%08x, moduleType=%u (%s), subEventSize=%u\n",
                               subEventHeader, moduleType, get_vme_module_name((VMEModuleType)moduleType),
                               subEventSize);

                        for (u32 i=0; i<subEventSize; ++i)
                        {
                            u32 subEventData;
                            infile.read((char *)&subEventData, sizeof(u32));

                            if (dumpData)
                                printf("    %2u = 0x%08x\n", i, subEventData);
                        }
                        wordsLeft -= subEventSize;
                    }

                    u32 eventEndMarker;
                    infile.read((char *)&eventEndMarker, sizeof(u32));
                    printf("   eventEndMarker=0x%08x\n", eventEndMarker);
                } break;

            case SectionType_Timetick:
                {
                    ++currentSecondInRun;
                    std::string str = read_string_data(infile, sectionBytes);
                    cout << "Timetick section: " << str << endl;
                    cout << "Second " << currentSecondInRun << " in the DAQ run begins" << endl;
                } break;

            case SectionType_End:
                {
                    std::string str = read_string_data(infile, sectionBytes);
                    cout << "End section: " << str << endl;
                    continueReading = false;

                    auto currentFilePos = infile.tellg();
                    infile.seekg(0, std::ifstream::end);
                    auto endFilePos = infile.tellg();

                    if (currentFilePos != endFilePos)
                    {
                        cout << "Warning: " << (endFilePos - currentFilePos)
                            << " bytes left after Listfile End Section" << endl;
                    }
                } break;

            case SectionType_Pause:
                {
                    if (sectionSize == 1)
                    {
                        u32 value = 0;
                        infile.read((char *)&value, sizeof(value));

                        switch (static_cast<PauseAction>(value))
                        {
                            case Pause:
                                cout << "Pause" << endl;
                                break;
                            case Resume:
                                cout << "Resume" << endl;
                        }
                    }
                    else
                    {
                        cout << "Pause section containing " << sectionSize << " words. "
                            << "Expected sectionSize to be 1 word!" << endl;
                        infile.seekg(sectionBytes, std::ifstream::cur);
                    }
                } break;

            default:
                {
                    printf("Warning: Unknown section type %u of size %u, skipping...\n",
                           sectionType, sectionSize);
                    infile.seekg(sectionBytes, std::ifstream::cur);
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

    infile.seekg(0, std::ifstream::beg);
    infile.read(fourCC, bytesToRead);

    cerr << "read fourCC:" << std::string(fourCC, bytesToRead) << endl;

    // Check if we have one of the MVLC files (8 magic bytes, either MVLC_ETH
    // or MVLC_USB. Those are not supported by this tool. Parsing the formats
    // needs a lot more work (packet loss, continuation frames, etc).
    if (std::strncmp(fourCC, "MVLC", bytesToRead) == 0)
        throw std::runtime_error(
            "Detected MVLC listfile format which is not supported by the listfile-dumper.");

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

    auto inputFilename = std::string(argv[1]);

    if (inputFilename.size() < 4
        || inputFilename.substr(inputFilename.size() - 4) == ".zip")
    {
        cerr << "Error: ZIP archives are not supported by this tool." << endl;
        return 1;
    }

    std::ifstream infile(inputFilename, std::ios::binary);

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
