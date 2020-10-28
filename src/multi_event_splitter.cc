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
#include "multi_event_splitter.h"

#include <algorithm>


#define LOG_LEVEL_OFF     0
#define LOG_LEVEL_WARN  100
#define LOG_LEVEL_INFO  200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MULTI_EVENT_SPLITTER_LOG_LEVEL
#define MULTI_EVENT_SPLITTER_LOG_LEVEL LOG_LEVEL_OFF
#endif

#define LOG_LEVEL_SETTING MULTI_EVENT_SPLITTER_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)\
do\
{\
    if (LOG_LEVEL_SETTING >= level)\
    {\
        fprintf(stderr, prefix "%s(): " fmt "\n", __FUNCTION__, ##__VA_ARGS__);\
    }\
} while (0);

#define LOG_WARN(fmt, ...)  DO_LOG(LOG_LEVEL_WARN,  "WARN - multi_event_splitter ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DO_LOG(LOG_LEVEL_INFO,  "INFO - multi_event_splitter ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - multi_event_splitter ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DO_LOG(LOG_LEVEL_TRACE, "TRACE - multi_event_splitter ", fmt, ##__VA_ARGS__)

namespace
{

class TheMultiEventSplitterErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "multi_event_splitter";
    }

    std::string message(int ev) const override
    {
        using mvme::multi_event_splitter::ErrorCode;

        switch (static_cast<ErrorCode>(ev))
        {
            case ErrorCode::Ok:
                return "No Error";

            case ErrorCode::EventIndexOutOfRange:
                return "Event index out of range";

            case ErrorCode::ModuleIndexOutOfRange:
                return "Module index out of range";
        }

        return "unrecognized multi_event_splitter error";
    }
};

const TheMultiEventSplitterErrorCategory theMultiEventSplitterErrorCategory {};

} // end anon namespace

namespace mvme
{
namespace multi_event_splitter
{

State make_splitter(const std::vector<std::vector<std::string>> &splitFilterStrings)
{
    State result;

    size_t eventMaxModules = 0u;

    for (const auto &moduleStrings: splitFilterStrings)
    {
        std::vector<State::FilterWithSizeCache> moduleFilters;

        for (auto moduleString: moduleStrings)
        {
            State::FilterWithSizeCache fc;

            fc.filter = a2::data_filter::make_filter(moduleString);
            fc.cache = a2::data_filter::make_cache_entry(fc.filter, 'S');

            moduleFilters.emplace_back(fc);
        }

        result.splitFilters.emplace_back(moduleFilters);

        eventMaxModules = std::max(moduleStrings.size(), eventMaxModules);
    }

    // Allocate space for the module data spans for each event
    result.dataSpans.resize(eventMaxModules);

    // For each event determine if splitting should be enabled. This is the
    // case if any of the events modules has a non-zero header filter.
    size_t eventIndex = 0;
    for (const auto &filters: result.splitFilters)
    {
        bool hasNonZeroFilter = std::any_of(
            filters.begin(), filters.end(),
            [] (const State::FilterWithSizeCache &fc)
            {
                return fc.filter.matchMask != 0;
            });

        result.enabledForEvent[eventIndex++] = hasNonZeroFilter;
    }

    assert(result.enabledForEvent.size() >= result.splitFilters.size());

    return result;
}

std::error_code begin_event(State &state, int ei)
{
    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans;

    std::fill(spans.begin(), spans.end(), State::ModuleDataSpans{});

    return {};
}

// The module_(prefix|dynamic|suffix) functions record the data pointer and
// size in the splitters state structure for later use in the end_event
// function.

std::error_code module_prefix(State &state, int ei, int mi, const u32 *data, u32 size)
{
    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans;

    if (mi >= static_cast<int>(spans.size()))
        return make_error_code(ErrorCode::ModuleIndexOutOfRange);

    spans[mi].prefixSpan = { data, data + size };

    LOG_TRACE("state=%p, ei=%d, mi=%d, data=%p, dataSize=%u",
              &state, ei, mi, data, size);

    return {};
}

std::error_code module_data(State &state, int ei, int mi, const u32 *data, u32 size)
{
    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans;

    if (mi >= static_cast<int>(spans.size()))
        return make_error_code(ErrorCode::ModuleIndexOutOfRange);

    spans[mi].dynamicSpan = { data, data + size };

    LOG_TRACE("state=%p, ei=%d, mi=%d, data=%p, dataSize=%u",
              &state, ei, mi, data, size);

    return {};
}

std::error_code module_suffix(State &state, int ei, int mi, const u32 *data, u32 size)
{
    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans;

    if (mi >= static_cast<int>(spans.size()))
        return make_error_code(ErrorCode::ModuleIndexOutOfRange);

    spans[mi].suffixSpan = { data, data + size };

    LOG_TRACE("state=%p, ei=%d, mi=%d, data=%p, dataSize=%u",
              &state, ei, mi, data, size);

    return {};
}

// Returns the number of words in the span or 0 in case any of the pointers is
// null or begin >= end.
inline size_t words_in_span(const State::DataSpan &span)
{
    if (span.begin && span.end && span.begin < span.end)
        return static_cast<size_t>(span.end - span.begin);

    return 0u;
}

std::error_code end_event(State &state, Callbacks &callbacks, int ei)
{
    // This is called after prefix, suffix and the dynamic part of all modules for
    // the given event have been recorded in the ModuleDataSpans. Now walk the data
    // and yield the subevents.

    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &moduleFilters = state.splitFilters[ei];
    auto &moduleSpans = state.dataSpans;
    const size_t moduleCount = moduleSpans.size();

    LOG_TRACE("state=%p, ei=%d, moduleCount=%lu", &state, ei, moduleCount);

    assert(moduleFilters.size() == moduleSpans.size());

    // If splitting is not enabled for this event yield the collected data in
    // one go.
    if (!state.enabledForEvent[ei])
    {
        LOG_TRACE("state=%p, splitting not enabled for ei=%d; invoking callbacks with non-split data",
                  &state, ei);

        callbacks.beginEvent(ei);

        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &spans = moduleSpans[mi];

            if (auto size = words_in_span(spans.prefixSpan))
                callbacks.modulePrefix(ei, mi, spans.prefixSpan.begin, size);

            if (auto size = words_in_span(spans.dynamicSpan))
                callbacks.moduleDynamic(ei, mi, spans.dynamicSpan.begin, size);

            if (auto size = words_in_span(spans.suffixSpan))
                callbacks.moduleSuffix(ei, mi, spans.suffixSpan.begin, size);
        }

        callbacks.endEvent(ei);

        return {};
    }

    bool staticPartsYielded = false;
    std::array<s64, MaxVMEModules> moduleSubeventSizes;

    // Space to record filter matches per module during the splitting phase.
    std::bitset<MaxVMEModules> moduleFilterMatches;

    assert(moduleSubeventSizes.size() >= moduleCount);
    assert(moduleFilterMatches.size() >= moduleCount);

    while (true)
    {
        // clear every bit to 0
        moduleFilterMatches.reset();

        // init sizes to -1
        std::fill(std::begin(moduleSubeventSizes), std::end(moduleSubeventSizes), -1);

        // Check for header matches and determine subevent sizes.
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            const auto &dynamicSpan = moduleSpans[mi].dynamicSpan;

            if (words_in_span(dynamicSpan))
            {
                bool hasMatch = a2::data_filter::matches(
                    moduleFilters[mi].filter, *dynamicSpan.begin);

                moduleFilterMatches[mi] = hasMatch;

                // The filter contains 'S' placeholders to directly extract the
                // number of following words from the header.
                if (hasMatch && moduleFilters[mi].cache.extractMask)
                {
                    // Add one to the extracted module event size to account for
                    // the header word itself (the extracted size is the number of
                    // words following the header word).
                    u32 moduleEventSize = 1 + a2::data_filter::extract(
                        moduleFilters[mi].cache, *dynamicSpan.begin);

                    moduleSubeventSizes[mi] = moduleEventSize;

                    LOG_TRACE("state=%p, ei=%d, mi=%lu, checked header '0x%08x', match=%s, hasSize=true, extractedSize=%u",
                              &state, ei, mi, *dynamicSpan.begin, hasMatch ? "true" : "false", moduleEventSize);
                }
                else if (hasMatch)
                {
                    // Determine the subevent size by checking when the module
                    // header filter matches again.
                    const u32 *moduleWord = dynamicSpan.begin + 1;

                    while (moduleWord < dynamicSpan.end)
                    {
                        if (a2::data_filter::matches(
                                moduleFilters[mi].filter, *moduleWord))
                        {
                            break;
                        }
                    }

                    u32 moduleEventSize = moduleWord - dynamicSpan.begin + 1;
                    moduleSubeventSizes[mi] = moduleEventSize;

                    LOG_TRACE("state=%p, ei=%d, mi=%lu, checked header '0x%08x', match=%s, hasSize=false, searchedSize=%u",
                              &state, ei, mi, *dynamicSpan.begin, hasMatch ? "true" : "false", moduleEventSize)
                }
            }
        }

        // Termination condition: none of the modules have any more dynamic
        // data left or the header filter did not match for any of them.
        if (moduleFilterMatches.none())
            break;

        LOG_TRACE("state=%p, callbacks.beginEvent(%d)", &state, ei);
        callbacks.beginEvent(ei);

        // Yield the prefixes of all the modules
        if (!staticPartsYielded)
        {
            for (size_t mi = 0; mi < moduleCount; ++mi)
            {
                if (auto size = words_in_span(moduleSpans[mi].prefixSpan))
                {
                    LOG_TRACE("state=%p, callbacks.modulePrefx(ei=%d, mi=%lu, data=%p, size=%lu",
                              &state, ei, mi, moduleSpans[mi].prefixSpan.begin, size);
                    callbacks.modulePrefix(ei, mi, moduleSpans[mi].prefixSpan.begin, size);
                }
            }
        }

        // Yield one split subevent for each of the modules.
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            if (moduleFilterMatches[mi])
            {
                auto &spans = moduleSpans[mi];

                // If there are no more words in the span then the bit
                // indicating a match should not have been set.
                assert(words_in_span(spans.dynamicSpan));

                // Add one to the extracted module event size to account for
                // the header word itself (the extracted size is the number of
                // words following the header word).
                u32 moduleEventSize = moduleSubeventSizes[mi];

                LOG_TRACE("state=%p, ei=%d, mi=%lu, words in dynamicSpan=%lu, moduleEventSize=%u",
                          &state, ei, mi, words_in_span(spans.dynamicSpan), moduleEventSize);

                if (moduleEventSize > words_in_span(spans.dynamicSpan))
                {
                    // The extracted event size exceeds the amount of data left
                    // in the dynamic span. Move the span begin pointer forward
                    // so that the span has size 0 and the module filter test
                    // above will fail on the next iteration.
                    spans.dynamicSpan.begin = spans.dynamicSpan.end;
                    continue;
                }

                // Invoke the dynamic data callback with the current dynamic
                // spans begin pointer and the extracted event size.
                LOG_TRACE("state=%p, callbacks.moduleDynamic(ei=%d, mi=%lu, data=%p, size=%u",
                          &state, ei, mi, spans.dynamicSpan.begin, moduleEventSize)

                callbacks.moduleDynamic(ei, mi, spans.dynamicSpan.begin, moduleEventSize);

                // Move the spans begin pointer forward by the amount of data used.
                spans.dynamicSpan.begin += moduleEventSize;

                if (words_in_span(spans.dynamicSpan))
                {
                    LOG_TRACE("state=%p, next dynamicSpan.begin=0x%08x",
                              &state, *spans.dynamicSpan.begin);
                }
            }
        }

        // Yield the suffixes of all the modules
        if (!staticPartsYielded)
        {
            for (size_t mi = 0; mi < moduleCount; ++mi)
            {
                if (auto size = words_in_span(moduleSpans[mi].suffixSpan))
                {
                    LOG_TRACE("state=%p, callbacks.moduleSuffix(ei=%d, mi=%lu, data=%p, size=%lu",
                              &state, ei, mi, moduleSpans[mi].suffixSpan.begin, size);
                    callbacks.moduleSuffix(ei, mi, moduleSpans[mi].suffixSpan.begin, size);
                }
            }

            staticPartsYielded = true;
        }
    }

    return {};
}

std::error_code LIBMVME_EXPORT make_error_code(ErrorCode error)
{
    return { static_cast<int>(error), theMultiEventSplitterErrorCategory };
}

} // end namespace multi_event_splitter
} // end namespace mvme
