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
#include "multi_event_splitter.h"

#include <algorithm>
#include <mesytec-mvlc/util/io_util.h>
#include <spdlog/spdlog.h>

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
        fprintf(stderr, prefix "%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);\
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
        using mesytec::mvme::multi_event_splitter::ErrorCode;

        switch (static_cast<ErrorCode>(ev))
        {
            case ErrorCode::Ok:
                return "Ok";

            case ErrorCode::EventIndexOutOfRange:
                return "Event index out of range";

            case ErrorCode::ModuleIndexOutOfRange:
                return "Module index out of range";

            case ErrorCode::MaxVMEEventsExceeded:
                return fmt::format("Maximum number of VME events ({}) exceeded", MaxVMEEvents);

            case ErrorCode::MaxVMEModulesExceeded:
                return fmt::format("Maximum number of VME modules ({}) exceeded", MaxVMEModules);
        }

        return "unrecognized multi_event_splitter error";
    }
};

const TheMultiEventSplitterErrorCategory theMultiEventSplitterErrorCategory {};

} // end anon namespace

namespace mesytec::mvme::multi_event_splitter
{

namespace
{
    Counters make_counters(const std::vector<std::vector<std::string>> &splitFilterStrings)
    {
        const size_t eventCount = splitFilterStrings.size();

        Counters counters;
        counters.inputEvents.resize(eventCount);
        counters.outputEvents.resize(eventCount);
        counters.inputModules.resize(eventCount);
        counters.outputModules.resize(eventCount);
        counters.moduleHeaderMismatches.resize(eventCount);
        counters.moduleEventSizeExceedsBuffer.resize(eventCount);

        for (size_t ei=0; ei<splitFilterStrings.size(); ++ei)
        {
            const size_t moduleCount = splitFilterStrings[ei].size();
            counters.inputModules[ei].resize(moduleCount);
            counters.outputModules[ei].resize(moduleCount);
            counters.moduleHeaderMismatches[ei].resize(moduleCount);
            counters.moduleEventSizeExceedsBuffer[ei].resize(moduleCount);
        }

        return counters;
    }
}

std::pair<State, std::error_code> make_splitter(const std::vector<std::vector<std::string>> &splitFilterStrings)
{
    auto result = std::pair<State, std::error_code>();
    auto &state = result.first;
    auto &ec = result.second;

    if (splitFilterStrings.size() > MaxVMEEvents)
    {
        ec = make_error_code(ErrorCode::MaxVMEEventsExceeded);
        return result;
    }

    for (size_t ei=0; ei<splitFilterStrings.size(); ++ei)
    {
        if (splitFilterStrings[ei].size() > MaxVMEModules)
        {
            ec = make_error_code(ErrorCode::MaxVMEModulesExceeded);
            return result;
        }
    }

    size_t eventMaxModules = 0u;

    for (const auto &moduleStrings: splitFilterStrings)
    {
        eventMaxModules = std::max(moduleStrings.size(), eventMaxModules);

        std::vector<mvlc::util::FilterWithCaches> moduleFilters;

        for (auto moduleString: moduleStrings)
            moduleFilters.emplace_back(mvlc::util::make_filter_with_caches(moduleString));

        state.splitFilters.emplace_back(moduleFilters);
    }

    // Allocate space for the module data spans for each event
    state.dataSpans.resize(eventMaxModules);

    // For each event determine if splitting should be enabled. This is the
    // case if any of the events modules has a non-zero header filter.
    size_t eventIndex = 0;
    for (const auto &filters: state.splitFilters)
    {
        bool hasNonZeroFilter = std::any_of(
            filters.begin(), filters.end(),
            [] (const auto &fc)
            {
                return fc.filter.matchMask != 0;
            });

        state.enabledForEvent[eventIndex++] = hasNonZeroFilter;
    }

    assert(state.enabledForEvent.size() >= state.splitFilters.size());

    state.counters = make_counters(splitFilterStrings);

    return std::make_pair(std::move(state), ec);
}

namespace
{

inline std::error_code begin_event(State &state, int ei)
{
    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans;

    std::fill(spans.begin(), spans.end(), State::ModuleData{});
    ++state.counters.inputEvents[ei];

    return {};
}

// The module_data() function records the data pointer and size in the
// splitters state structure for later use in the end_event function.
inline std::error_code module_data(State &state, int ei, int mi, const ModuleData &moduleData)
{
    if (ei >= static_cast<int>(state.splitFilters.size()))
    {
        ++state.counters.eventIndexOutOfRange;
        return make_error_code(ErrorCode::EventIndexOutOfRange);
    }

    auto &spans = state.dataSpans;

    if (mi >= static_cast<int>(spans.size()))
    {
        ++state.counters.moduleIndexOutOfRange;
        return make_error_code(ErrorCode::ModuleIndexOutOfRange);
    }

    spans[mi] = moduleData;

    LOG_TRACE("state=%p, ei=%d, mi=%d, data=%p, dataSize=%u",
              &state, ei, mi, moduleData.data.data, moduleData.data.size);

    ++state.counters.inputModules[ei][mi];

    return {};
}

#if 1
std::error_code end_event1_(State &state, Callbacks &callbacks, void *userContext,
    const int ei, const unsigned moduleCount)
{
    // This is called after prefix, suffix and the dynamic part of all modules for
    // the given event have been recorded in the ModuleDataSpans. Now walk the data
    // and yield the subevents.

    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei > MaxVMEEvents)
        return make_error_code(ErrorCode::MaxVMEEventsExceeded);

    if (moduleCount > MaxVMEModules)
        return make_error_code(ErrorCode::MaxVMEModulesExceeded);

    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &moduleFilters = state.splitFilters[ei];
    auto &moduleSpans = state.dataSpans;

    LOG_TRACE("state=%p, ei=%d, moduleCount=%u", &state, ei, moduleCount);

    // If splitting is not enabled for this event yield the collected data in
    // one go.
    if (!state.enabledForEvent[ei])
    {
        LOG_TRACE("state=%p, splitting not enabled for ei=%d; invoking callbacks with non-split data",
                  &state, ei);

        const int crateIndex = 0; // FIXME: do not hardcode this. pass it along at least. make it an arg to end_event() or keep it in the state.
        callbacks.eventData(userContext, crateIndex, ei, state.dataSpans.data(), moduleCount);

        for (size_t mi = 0; mi < moduleCount; ++mi)
            ++state.counters.outputModules[ei][mi];
        ++state.counters.outputEvents[ei];

        return {};
    }

    assert(moduleFilters.size() <= moduleSpans.size());

    std::array<s64, MaxVMEModules+1> moduleSubeventSizes;

    // Space to record filter matches per module during the splitting phase.
    std::bitset<MaxVMEModules+1> moduleFilterMatches;

    // ModuleData structures used when invoking the eventData callback.
    std::array<ModuleData, MaxVMEModules+1> moduleDataList;
    std::fill(std::begin(moduleDataList), std::end(moduleDataList), ModuleData{});

    assert(moduleSubeventSizes.size() > moduleCount);
    assert(moduleFilterMatches.size() > moduleCount);

    while (true)
    {
        // clear every bit to 0
        moduleFilterMatches.reset();

        // init sizes to -1
        std::fill(std::begin(moduleSubeventSizes), std::end(moduleSubeventSizes), -1);

        // Check for header matches and determine subevent sizes.
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            const auto &dynamicSpan = moduleSpans[mi].dataSpan;

            if (words_in_span(dynamicSpan))
            {
                bool hasMatch = mvlc::util::matches(
                    moduleFilters[mi].filter, *dynamicSpan.begin);

                moduleFilterMatches[mi] = hasMatch;

                // The filter contains 'S' placeholders to directly extract the
                // number of following words from the header.
                if (hasMatch)
                {
                    if (auto cache = mvlc::util::get_cache_entry(moduleFilters[mi], 'S'))
                    {
                        // Add one to the extracted module event size to account for
                        // the header word itself (the extracted size is the number of
                        // words following the header word).
                        u32 moduleEventSize = 1 + mvlc::util::extract(*cache, *dynamicSpan.begin);

                        moduleSubeventSizes[mi] = moduleEventSize;

                        LOG_TRACE("state=%p, ei=%d, mi=%lu, checked header '0x%08x', match=%s, hasSize=true, extractedSize+1=%u",
                                &state, ei, mi, *dynamicSpan.begin, hasMatch ? "true" : "false", moduleEventSize);
                    }
                    else
                    {
                        // Determine the subevent size by checking when the module
                        // header filter matches again.
                        const u32 *moduleWord = dynamicSpan.begin + 1;

                        while (moduleWord < dynamicSpan.end)
                        {
                            if (mvlc::util::matches(moduleFilters[mi].filter, *moduleWord))
                                break;
                            ++moduleWord;
                        }

                        u32 moduleEventSize = moduleWord - dynamicSpan.begin;
                        moduleSubeventSizes[mi] = moduleEventSize;

                        LOG_TRACE("state=%p, ei=%d, mi=%lu, checked header '0x%08x', match=%s, hasSize=false, searchedSize=%u",
                                &state, ei, mi, *dynamicSpan.begin, hasMatch ? "true" : "false", moduleEventSize)
                    }
                }
                else
                {
                    bool hasSizeMask = mvlc::util::get_cache_entry(moduleFilters[mi], 'S') != std::nullopt;

                    LOG_WARN("state=%p, ei=%d, mi=%lu, checked header '0x%08x', no match!, hasSizeMask=%s",
                             &state, ei, mi, *dynamicSpan.begin, hasSizeMask ? "true" : "false");
                    ++state.counters.moduleHeaderMismatches[ei][mi];
                    state.processingFlags |= State::ProcessingFlags::ModuleHeaderMismatch;

                    if (callbacks.logger)
                    {
                        const auto spanLen = dynamicSpan.end - dynamicSpan.begin;
                        std::ostringstream os;
                        mesytec::mvlc::util::log_buffer(
                            os, dynamicSpan.begin, spanLen,
                            fmt::format("module header mismatch: ei={}, mi={}, len={}", ei, mi, spanLen));
                        callbacks.logger(userContext, os.str());
                    }
                }
            }
        }

        // Termination condition: none of the modules have any more dynamic
        // data left or the header filter did not match for any of them.
        if (moduleFilterMatches.none())
            break;

        // Yield one split subevent for each of the modules.
        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &moduleData = moduleDataList[mi];
            moduleData = {};

            if (moduleFilterMatches[mi])
            {
                auto &spans = moduleSpans[mi];

                // If there are no more words in the span then the bit
                // indicating a match should not have been set.
                assert(words_in_span(spans.dataSpan));

                u32 moduleEventSize = moduleSubeventSizes[mi];

                LOG_TRACE("state=%p, ei=%d, mi=%lu, words in dataSpan=%lu, moduleEventSize=%u",
                          &state, ei, mi, words_in_span(spans.dataSpan), moduleEventSize);


                if (moduleEventSize > words_in_span(spans.dataSpan))
                {
                    // The extracted size exceeds the actual amount of data left
                    // in the buffer. Yield all available data and record the
                    // error.
                    moduleData.data = { spans.dataSpan.begin, static_cast<u32>(words_in_span(spans.dataSpan)) };
                    moduleData.dynamicSize = moduleData.data.size;
                    moduleData.hasDynamic = true;

                    ++state.counters.moduleEventSizeExceedsBuffer[ei][mi];
                    state.processingFlags |= State::ProcessingFlags::ModuleSizeExceedsBuffer;

                    if (callbacks.logger)
                    {
                        const auto spanLen = words_in_span(spans.dataSpan);
                        std::ostringstream os;
                        mesytec::mvlc::util::log_buffer(
                            os, spans.dataSpan.begin, spanLen,
                            fmt::format("module data size ({}) exceeds buffer size: ei={}, mi={}, bufferSize={}",
                                moduleEventSize, ei, mi, spanLen));
                        callbacks.logger(userContext, os.str());
                    }

                    // Move the spans begin pointer forward by the amount of data used.
                    spans.dataSpan.begin += words_in_span(spans.dataSpan);
                }
                else
                {
                    // Invoke the dynamic data callback with the current dynamic
                    // spans begin pointer and the extracted event size.
                    LOG_TRACE("state=%p, callbacks.moduleData(ei=%d, mi=%lu, data=%p, size=%u",
                              &state, ei, mi, spans.dataSpan.begin, moduleEventSize)

                        //callbacks.moduleDynamic(ei, mi, spans.dynamicSpan.begin, moduleEventSize);
                        moduleData.data = { spans.dataSpan.begin, moduleEventSize };
                        moduleData.dynamicSize = moduleData.data.size;
                        moduleData.hasDynamic = true;

                    // Move the spans begin pointer forward by the amount of data used.
                    spans.dataSpan.begin += moduleEventSize;

                    if (words_in_span(spans.dataSpan))
                    {
                        LOG_TRACE("state=%p, next dynamicSpan.begin=0x%08x",
                                  &state, *spans.dataSpan.begin);
                    }
                }

                assert(mvlc::readout_parser::size_consistency_check(moduleData));

                ++state.counters.outputModules[ei][mi];
            }
        }

        ++state.counters.outputEvents[ei];

        if (LOG_LEVEL_SETTING >= LOG_LEVEL_TRACE)
        {
            auto inEvents = state.counters.inputEvents[ei];
            auto outEvents =state.counters.outputEvents[ei];
            auto ratio = outEvents * 1.0 / inEvents;

            LOG_TRACE("event out/in ratio=%f, outEvents=%lu, inEvents=%lu", ratio, outEvents, inEvents);
        }

        int crateIndex = 0;
        callbacks.eventData(userContext, crateIndex, ei, moduleDataList.data(), moduleCount);
    }

    return {};
}
#endif

std::error_code end_event2_(State &state, Callbacks &callbacks, void *userContext,
    const int ei, const unsigned moduleCount)
{
    // This is called after module data for the given event has been recorded.
    // Now walk the data and yield split events.

    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei > MaxVMEEvents)
        return make_error_code(ErrorCode::MaxVMEEventsExceeded);

    if (moduleCount > MaxVMEModules)
        return make_error_code(ErrorCode::MaxVMEModulesExceeded);

    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    LOG_TRACE("state=%p, ei=%d, moduleCount=%u", &state, ei, moduleCount);

    // If splitting is not enabled for this event yield the collected data in
    // one go.
    if (!state.enabledForEvent[ei])
    {
        LOG_TRACE("state=%p, splitting not enabled for ei=%d; invoking callbacks with non-split data",
                  &state, ei);

        const int crateIndex = 0; // FIXME: do not hardcode this. pass it along at least. make it an arg to end_event() or keep it in the state.
        callbacks.eventData(userContext, crateIndex, ei, state.dataSpans.data(), moduleCount);

        for (size_t mi = 0; mi < moduleCount; ++mi)
            ++state.counters.outputModules[ei][mi];
        ++state.counters.outputEvents[ei];

        return {};
    }

    auto &moduleFilters = state.splitFilters[ei];
    auto &moduleData = state.dataSpans;

    assert(moduleFilters.size() <= moduleData.size());

    return {};
}

std::error_code end_event(State &state, Callbacks &callbacks, void *userContext,
    const int ei, const unsigned moduleCount)
{
    return end_event2_(state, callbacks, userContext, ei, moduleCount);
}

} // end anon namespace

std::error_code LIBMVME_EXPORT event_data(
    State &state, Callbacks &callbacks,
    void *userContext, int ei, const ModuleData *moduleDataList, unsigned moduleCount)
{
    if (auto ec = begin_event(state, ei))
        return ec;

    for (unsigned mi=0; mi<moduleCount; ++mi)
    {
        auto &moduleData = moduleDataList[mi];

        if (auto ec = module_data(state, ei, mi, moduleData))
            return ec;
    }

    return end_event(state, callbacks, userContext, ei, moduleCount);
}

std::error_code LIBMVME_EXPORT make_error_code(ErrorCode error)
{
    return { static_cast<int>(error), theMultiEventSplitterErrorCategory };
}

template<typename Out>
Out &format_counters_(Out &out, const Counters &counters)
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

std::ostream &format_counters(std::ostream &out, const Counters &counters)
{
    return format_counters_(out, counters);
}

template<typename Out>
Out &format_counters_tabular_(Out &out, const Counters &counters)
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

std::ostream &format_counters_tabular(std::ostream &out, const Counters &counters)
{
    return format_counters_tabular_(out, counters);
}

} // end namespace multi_event_splitter
