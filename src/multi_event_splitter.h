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

#include <boost/dynamic_bitset.hpp>
#include <functional>
#include <string>
#include <vector>

#include "libmvme_export.h"

#include "typedefs.h"
#include "analysis/a2/a2.h"

namespace mvme
{
namespace multi_event_splitter
{

// IMPORTANT: The multi_event_splitter requires that the pointers passed to
// module_data calls are still valid when end_event is called. The code stores
// the pointers and sizes to run the splitting logic on the data in end_event()
// and invoke the given callbacks. The data is NOT copied into a separate buffer!

struct Callbacks
{
    // functions taking an event index
    std::function<void (int ei)>
        beginEvent = [] (int) {},
        endEvent   = [] (int) {};

    // Parameters: event index, module index, pointer to first word, number of words
    std::function<void (int ei, int mi, const u32 *data, u32 size)>
        modulePrefix = [] (int, int, const u32*, u32) {},
        moduleDynamic = [] (int, int, const u32*, u32) {},
        moduleSuffix = [] (int, int, const u32*, u32) {};
};

struct State
{
    struct FilterWithSizeCache
    {
        a2::data_filter::DataFilter filter;
        a2::data_filter::CacheEntry cache;
        bool hasSize;
    };

    struct DataSpan
    {
        const u32 *begin;
        const u32 *end;
    };

    struct ModuleDataSpans
    {
        DataSpan prefixSpan;
        DataSpan dynamicSpan;
        DataSpan suffixSpan;
    };

    // DataFilters used for module header matching and size extraction grouped
    // by event and module indexes.
    std::vector<std::vector<FilterWithSizeCache>> splitFilters;

    // Storage to record pointers into the incoming (multievent) module data.
    std::vector<std::vector<ModuleDataSpans>> dataSpans;

    // Bit N is set if splitting is enabled for corresponding event index.
    boost::dynamic_bitset<> enabledForEvent;

    // Space to record filter matches per module during the splitting phase.
    boost::dynamic_bitset<> moduleFilterMatches;
};

// Creates an initial splitter state. The input are lists of per event and
// module data_filter strings used to detect module headers and extract module
// data sizes.
// The filter strings are used to create a2::data_filter::DataFilter
// structures. A filter match is attempted for each potential module header. If
// the filter matches then the modules data size in number of words is
// extracted from the filter using the filter characters 'S' or 's'.
// If there is no filter match the algorithm assumes that there are no more
// events available for that module.
//
// If the filter does not contain any 'S' placeholders the length of the
// subevent is determined by testing each of the following words for a filter
// match. The matched word is assumed to be the header of the next event, all
// data words before that are part of the current event.
State make_splitter(const std::vector<std::vector<std::string>> &splitFilterStrings);

enum class ErrorCode: u8
{
    Ok,
    EventIndexOutOfRange,
    ModuleIndexOutOfRange,
};

std::error_code LIBMVME_EXPORT begin_event(State &state, int ei);
std::error_code LIBMVME_EXPORT module_prefix(State &state, int ei, int mi, const u32 *data, u32 size);
std::error_code LIBMVME_EXPORT module_data(State &state, int ei, int mi, const u32 *data, u32 size);
std::error_code LIBMVME_EXPORT module_suffix(State &state, int ei, int mi, const u32 *data, u32 size);
std::error_code LIBMVME_EXPORT end_event(State &state, Callbacks &callbacks, int ei);
std::error_code LIBMVME_EXPORT end_event2(State &state, Callbacks &callbacks, int ei);

std::error_code LIBMVME_EXPORT make_error_code(ErrorCode error);

} // end namespace multi_event_splitter
} // end namespace mvme

namespace std
{
    template<> struct is_error_code_enum<mvme::multi_event_splitter::ErrorCode>: true_type {};
} // end namespace std

#endif /* __MVME_MULTI_EVENT_SPLITTER_H__ */
