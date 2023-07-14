/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
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
#ifndef __MVME_MULTI_EVENT_SPLITTER_H__
#define __MVME_MULTI_EVENT_SPLITTER_H__

#include <bitset>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <mesytec-mvlc/mvlc_readout_parser.h>
#include <mesytec-mvlc/util/fmt.h>


#include "libmvme_export.h"

#include "typedefs.h"
#include "analysis/a2/a2_data_filter.h"
#include "vme_config_limits.h"

namespace mesytec
{
namespace mvme
{
namespace multi_event_splitter
{

/*
 * The purpose of this code is to split module multievent data obtained via VME
 * block reads into separate single events.
 *
 * The supported readout structure is the same as that supported by the mvlc
 * readout parser: one fixed size prefix, a dynamic block read part and a fixed
 * size suffix per module. Splitting is performed on the dynamic part as shown
 * in the diagram below. m0 and m1 are the modules, the dynamically sized event
 * data for each module is obtained via a single block read.
 *
 *   multievent
 *  +-----------+
 *  | m0_prefix |                 split0          split1           split2
 *  | m0_event0 |              +------------+   +-----------+   +-----------+
 *  | m0_event1 |              | m0_prefix  |   |           |   |           |
 *  | m0_event2 |              | m1_prefix  |   |           |   |           |
 *  | m0_suffix |              | m0_event0  |   | m0_event1 |   | m0_event2 |
 *  |           | == split ==> | m1_event0  |   | m1_event1 |   | m1_event2 |
 *  | m1_prefix |              | m0_suffix  |   |           |   |           |
 *  | m1_event0 |              | m1_suffix  |   |           |   |           |
 *  | m1_event1 |              |            |   |           |   |           |
 *  | m1_event2 |              +------------+   +-----------+   +-----------+
 *  | m1_suffix |
 *  +-----------+
 *
 * In the example the input multievent data obtained from a single event
 * readout cycle is split into three separate events. Prefix and suffix data of
 * the modules are only yielded for the first event.
 *
 * The splitter is driven via the event_data() function. Output data is made
 * available through the Callbacks:eventData callback passed to event_data().
 *
 * Data splitting is performed by using analysis DataFilters to look for module
 * header words. If the filter contains the matching character 'S' it is used
 * to extract the size in words of the following event. Otherwise each of the
 * following input data words is tried until the header filter matches again
 * and the data in-between the two header words is assumed to be the single
 * event data.
 *
 *   +-----------+
 *   |m0_header  | <- Filter matches here. Extract event size if 'S' character in filter,
 *   |m0_e0_word0|    otherwise try the following words until another match is found or
 *   |m0_e0_word1|    there is no more input data left.
 *   |m0_e0_word2|
 *   |m0_header1 | <- Filter matches again
 *   |m0_e1_word0|
 *   |m0_e1_word1|
 *   |m0_e1_word2|
 *   +-----------+
 */


using ModuleData = mesytec::mvlc::readout_parser::ModuleData;

struct Callbacks
{
    // Required event data callback.
    std::function<void (void *userContext, int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)>
        eventData = [] (void *, int, int, const ModuleData *, unsigned) {};

    // Optional logger callback.
    std::function<void (void *userContext, const std::string &msg)>
        logger = [] (void *, const std::string &) {};
};

struct Counters
{
    std::vector<size_t> inputEvents; // number of input events by zero based event index
    std::vector<size_t> outputEvents; // numer of output events by zero based event index
    std::vector<std::vector<size_t>> inputModules; // [eventIndex, moduleIndex] -> number of input events per module
    std::vector<std::vector<size_t>> outputModules; // [eventIndex, moduleIndex] -> number of output events per module
    size_t eventIndexOutOfRange = 0;
    size_t moduleIndexOutOfRange = 0;

    // [eventIndex, moduleIndex] -> number of times the header filter used for
    // splitting did not match.
    std::vector<std::vector<size_t>> moduleHeaderMismatches;

    // [eventIndex, moduleIndex] -> number of times the module data size
    // extracted from the module header exceeds the amount of data in the input
    // buffer.
    std::vector<std::vector<size_t>> moduleEventSizeExceedsBuffer;
};

struct State
{
    struct FilterWithSizeCache
    {
        // Filter used for header matching and optional size extraction
        a2::data_filter::DataFilter filter;
        // Cache for the 'S' filter character.
        a2::data_filter::CacheEntry cache;
    };

    struct DataSpan
    {
        const u32 *begin;
        const u32 *end;
    };

    struct ModuleDataSpans
    {
        DataSpan dataSpan;
    };

    struct ProcessingFlags
    {
        // Set if the next expected module header word did not match the modules
        // header filter.
        static const u32 ModuleHeaderMismatch = 1u << 0;

        // Set if the extracted or calculated module data size exceeds the input
        // buffer size.
        static const u32 ModuleSizeExceedsBuffer = 1u << 1;
    };

    // DataFilters used for module header matching and size extraction grouped
    // by event and module indexes.
    std::vector<std::vector<FilterWithSizeCache>> splitFilters;

    // Storage to record pointers into the incoming (multievent) module data.
    std::vector<ModuleDataSpans> dataSpans;

    // Bit N is set if splitting is enabled for corresponding event index.
    std::bitset<MaxVMEEvents+1> enabledForEvent;

    Counters counters;

    // Used to communicate non-fatal error/warning conditions to the outside.
    // Reset this before calling event_data(), then examine the value to
    // determine if error/warning cases occured.
    u32 processingFlags = {};
};

// Creates an initial splitter state. The input are lists of per event and
// module data_filter strings used to detect module headers and extract module
// data sizes.
//
// Example:
// { { "0101 SSSS", "1010 XXXX" }, { "1111 SSSS", "0001 SSSS" } }
//
// Creates a splitter for two events, with two modules each. The second module
// of the first event doesn't have the 'S' match character so repeated
// matching will be performed to find the next header. For the other modules
// the event size can be directly extracted using the filter.
std::pair<State, std::error_code> make_splitter(const std::vector<std::vector<std::string>> &splitFilterStrings);

enum class ErrorCode : u8
{
    Ok,
    // event_data() was called with an event index >= the number of events in the splitFilterString vector
    EventIndexOutOfRange,
    ModuleIndexOutOfRange,
    MaxVMEEventsExceeded,
    MaxVMEModulesExceeded,
};

// The main multi_event_splitter entry point taking a parsed module data list.
std::error_code LIBMVME_EXPORT event_data(
    State &state, Callbacks &callbacks,
    void *userContext, int ei, const ModuleData *moduleDataList, unsigned moduleCount);


std::error_code LIBMVME_EXPORT make_error_code(ErrorCode error);

template<typename Out>
Out &format_counters(Out &out, const Counters &counters)
{
    for (size_t ei=0; ei<counters.inputEvents.size(); ++ei)
    {
        auto eventRatio = counters.outputEvents[ei] * 1.0 / counters.inputEvents[ei];
        out << fmt::format("* eventIndex={}, inputEvents={}, outputEvents={}, out/in={:.2f}\n",
            ei, counters.inputEvents[ei], counters.outputEvents[ei], eventRatio);

        for (size_t mi=0; mi<counters.inputModules[ei].size(); ++mi)
        {
            auto moduleRatio = counters.outputModules[ei][mi] * 1.0 / counters.inputModules[ei][mi];
            out << fmt::format("  - moduleIndex={}, inputModuleCount={}, outputModuleCount={}, out/in={:.2f}\n",
                mi, counters.inputModules[ei][mi], counters.outputModules[ei][mi], moduleRatio);
        }
    }

    out << fmt::format("* Error Counts: eventIndexOutOfRange={}, moduleIndexOutOfRange={}\n",
        counters.eventIndexOutOfRange, counters.moduleIndexOutOfRange);

    return out;
}

template<typename Out>
Out &format_counters_tabular(Out &out, const Counters &counters)
{
    out << fmt::format("{: <12} {: >12} {: >12} {: >12}\n", "Type", "InputCount", "OutputCount", "Out/In");

    for (size_t ei=0; ei<counters.inputEvents.size(); ++ei)
    {
        auto eventRatio = counters.outputEvents[ei] * 1.0 / counters.inputEvents[ei];

        out << fmt::format("{: <12} {: >12L} {: >12L} {: >12.2Lf}\n",
            fmt::format("event{}", ei),
            counters.inputEvents[ei],
            counters.outputEvents[ei],
            eventRatio
            );

        for (size_t mi=0; mi<counters.inputModules[ei].size(); ++mi)
        {
            auto moduleRatio = counters.outputModules[ei][mi] * 1.0 / counters.inputModules[ei][mi];
            out << fmt::format("{: <12} {: >12L} {: >12L} {: >12.2Lf}\n",
                fmt::format("module{}", mi),
                counters.inputModules[ei][mi],
                counters.outputModules[ei][mi],
                moduleRatio
                );
        }
    }

    out << fmt::format("\n{: <22} {: >12}\n", "ErrorType", "ErrorCount");
    out << fmt::format("{: <22} {: >12}\n", "eventIndexOutOfRange", counters.eventIndexOutOfRange);
    out << fmt::format("{: <22} {: >12}\n", "moduleIndexOutOfRange", counters.moduleIndexOutOfRange);

    out << "\nModule header mismatches:\n";
    for (size_t ei=0; ei<counters.moduleHeaderMismatches.size(); ++ei)
    {
        const auto &mismatches = counters.moduleHeaderMismatches[ei];
        for (size_t mi=0; mi<mismatches.size(); ++mi)
        {
            out << fmt::format("  {} {}\n",
                               fmt::format("event{}, module{}", ei, mi),
                               mismatches[mi]);
        }
    }

    out << "\nModule size exceeds input event size:\n";
    for (size_t ei=0; ei<counters.moduleEventSizeExceedsBuffer.size(); ++ei)
    {
        const auto &exceeds = counters.moduleEventSizeExceedsBuffer[ei];
        for (size_t mi=0; mi<exceeds.size(); ++mi)
        {
            out << fmt::format("  {} {}\n",
                               fmt::format("event{}, module{}", ei, mi),
                               exceeds[mi]);
        }
    }

    return out;
}


} // end namespace multi_event_splitter
} // end namespace mvme
} // end namespace mesytec

namespace std
{
    template<> struct is_error_code_enum<mesytec::mvme::multi_event_splitter::ErrorCode>: true_type {};
} // end namespace std

#endif /* __MVME_MULTI_EVENT_SPLITTER_H__ */
