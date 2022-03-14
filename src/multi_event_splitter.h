/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include <string>
#include <system_error>
#include <vector>
#include <mesytec-mvlc/mvlc_readout_parser.h>

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
 * The splitter is steered via the begin_event, module_prefix, module_data,
 * module_suffix and end_event functions. Output data is made available via the
 * functions in the Callback structure passed to end_event().
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
 *
 * IMPORTANT: The multi_event_splitter requires that the pointers passed to the
 * module_prefix(), module_data() and module_suffix() calls are still valid
 * when end_event is called. The code stores the pointers and sizes and then
 * performs event splitting and invocation of callbacks in end_event(). No
 * copies of the data are made.
 */


// Callbacks for the multi event splitter to hand module data to consumers.
// All module data callbacks are contained between calls to beginEvent and
// endEvent.
// Event splitting is performed on the dynamic part of incoming module data.
// Prefix and suffix data are not split. The prefix and suffix callbacks are
// only called once per incoming event whereas the dynamic part is called once
// for every split.

using ModuleData = mesytec::mvlc::readout_parser::ModuleData;

struct Callbacks
{
    std::function<void (void *userContext, int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)>
        eventData = [] (void *, int, int, const ModuleData *, unsigned) {};
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

    // DataFilters used for module header matching and size extraction grouped
    // by event and module indexes.
    std::vector<std::vector<FilterWithSizeCache>> splitFilters;

    // Storage to record pointers into the incoming (multievent) module data.
    std::vector<ModuleDataSpans> dataSpans;

    // Bit N is set if splitting is enabled for corresponding event index.
    std::bitset<MaxVMEEvents> enabledForEvent;
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
State make_splitter(const std::vector<std::vector<std::string>> &splitFilterStrings);

enum class ErrorCode: u8
{
    Ok,
    EventIndexOutOfRange,
    ModuleIndexOutOfRange,
};

std::error_code LIBMVME_EXPORT event_data(
    State &state, Callbacks &callbacks,
    void *userContext, int ei, const ModuleData *moduleDataList, unsigned moduleCount);


std::error_code LIBMVME_EXPORT make_error_code(ErrorCode error);

} // end namespace multi_event_splitter
} // end namespace mvme
} // end namespace mesytec

namespace std
{
    template<> struct is_error_code_enum<mesytec::mvme::multi_event_splitter::ErrorCode>: true_type {};
} // end namespace std

#endif /* __MVME_MULTI_EVENT_SPLITTER_H__ */
