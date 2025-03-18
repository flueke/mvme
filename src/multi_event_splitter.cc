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

#define LOG_LEVEL_OFF 0
#define LOG_LEVEL_WARN 100
#define LOG_LEVEL_INFO 200
#define LOG_LEVEL_DEBUG 300
#define LOG_LEVEL_TRACE 400

#ifndef MULTI_EVENT_SPLITTER_LOG_LEVEL
#define MULTI_EVENT_SPLITTER_LOG_LEVEL LOG_LEVEL_OFF
#endif

#define LOG_LEVEL_SETTING MULTI_EVENT_SPLITTER_LOG_LEVEL

#define DO_LOG(level, prefix, fmt, ...)                                                            \
    do                                                                                             \
    {                                                                                              \
        if (LOG_LEVEL_SETTING >= level)                                                            \
        {                                                                                          \
            fprintf(stderr, prefix "%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);         \
        }                                                                                          \
    }                                                                                              \
    while (0);

#define LOG_WARN(fmt, ...)                                                                         \
    DO_LOG(LOG_LEVEL_WARN, "WARN - multi_event_splitter ", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                                                                         \
    DO_LOG(LOG_LEVEL_INFO, "INFO - multi_event_splitter ", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)                                                                        \
    DO_LOG(LOG_LEVEL_DEBUG, "DEBUG - multi_event_splitter ", fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...)                                                                        \
    DO_LOG(LOG_LEVEL_TRACE, "TRACE - multi_event_splitter ", fmt, ##__VA_ARGS__)

namespace
{

class TheMultiEventSplitterErrorCategory: public std::error_category
{
    const char *name() const noexcept override { return "multi_event_splitter"; }

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

const TheMultiEventSplitterErrorCategory theMultiEventSplitterErrorCategory{};

} // namespace

namespace mesytec::mvme::multi_event_splitter
{

const u32 ProcessingFlags::Ok;
const u32 ProcessingFlags::ModuleHeaderMismatch;
const u32 ProcessingFlags::ModuleSizeExceedsBuffer;

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

    for (size_t ei = 0; ei < splitFilterStrings.size(); ++ei)
    {
        const size_t moduleCount = splitFilterStrings[ei].size();
        counters.inputModules[ei].resize(moduleCount);
        counters.outputModules[ei].resize(moduleCount);
        counters.moduleHeaderMismatches[ei].resize(moduleCount);
        counters.moduleEventSizeExceedsBuffer[ei].resize(moduleCount);
    }

    return counters;
}
} // namespace

std::pair<State, std::error_code>
make_splitter(const std::vector<std::vector<std::string>> &splitFilterStrings, int outputCrateIndex)
{
    auto result = std::pair<State, std::error_code>();
    auto &state = result.first;
    auto &ec = result.second;

    state.outputCrateIndex = outputCrateIndex;

    if (splitFilterStrings.size() > MaxVMEEvents)
    {
        ec = make_error_code(ErrorCode::MaxVMEEventsExceeded);
        return result;
    }

    for (size_t ei = 0; ei < splitFilterStrings.size(); ++ei)
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
        bool hasNonZeroFilter =
            std::any_of(filters.begin(), filters.end(),
                        [](const auto &fc) { return fc.filter.matchMask != 0; });

        state.enabledForEvent[eventIndex++] = hasNonZeroFilter;
    }

    assert(state.enabledForEvent.size() >= state.splitFilters.size());

    state.counters = make_counters(splitFilterStrings);

    return std::make_pair(std::move(state), ec);
}

inline std::error_code begin_event(State &state, int ei)
{
    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei >= static_cast<int>(state.splitFilters.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans;

    std::fill(spans.begin(), spans.end(), ModuleData{});
    ++state.counters.inputEvents[ei];

    return {};
}

// No suffix handling!
// Returns ProcessingFlags
inline u32 split_dynamic_part(const mesytec::mvlc::util::FilterWithCaches &filter,
                              const ModuleData &input,
                              std::vector<ModuleDataWithDebugInfo> &output)
{
    assert(input.hasDynamic && input.dynamicSize > 0);

    const auto filterSizeCache = mesytec::mvlc::util::get_cache_entry(filter, 'S');

    std::basic_string_view<u32> data(dynamic_span(input).data, dynamic_span(input).size);
    assert(!data.empty());

    ModuleDataWithDebugInfo current = {};
    current.md = input;

    u32 result = 0;

    while (!data.empty())
    {
        if (mesytec::mvlc::util::matches(filter.filter, data.front()))
        {
            if (filterSizeCache)
            {
                u32 size = 1 + mesytec::mvlc::util::extract(*filterSizeCache, data.front());

                if (size > data.size())
                {
                    result |= ProcessingFlags::ModuleSizeExceedsBuffer;
                    current.flags |= ProcessingFlags::ModuleSizeExceedsBuffer;
                }

                current.md.dynamicSize = std::min(size, static_cast<u32>(data.size()));
                current.md.hasDynamic = true;
                data.remove_prefix(current.md.dynamicSize);
            }
            else
            {
                current.md.dynamicSize = 1;
                data.remove_prefix(1);

                while (!data.empty())
                {
                    if (mesytec::mvlc::util::matches(filter.filter, data.front()))
                        break;

                    ++current.md.dynamicSize;
                    data.remove_prefix(1);
                }

                current.md.hasDynamic = true;
            }

            current.md.data.size = current.md.prefixSize + current.md.dynamicSize;

            assert(size_consistency_check(current.md));
            output.push_back(current);
        }
        else
        {
            if (filterSizeCache)
            {
                result |= ProcessingFlags::ModuleHeaderMismatch;
                current.flags |= ProcessingFlags::ModuleHeaderMismatch;
            }

            // no header match: consume all remaining data
            current.md.dynamicSize = data.size();
            current.md.hasDynamic = true;
            data.remove_prefix(data.size());

            assert(size_consistency_check(current.md));
            output.push_back(current);
        }

        current = {};
        current.md.data.data = data.data();
        current.md.data.size = data.size();
    }

    return result;
}

u32 split_module_data(const mesytec::mvlc::util::FilterWithCaches &filter,
                      const ModuleData &input, std::vector<ModuleDataWithDebugInfo> &output)
{
    // handle prefix-only and all empty-dynamic cases (with or without prefix/suffix)
    if (!input.hasDynamic || input.dynamicSize == 0)
    {
        ModuleDataWithDebugInfo current = {};
        current.md = input;
        output.emplace_back(current);
        return 0;
    }

    assert(size_consistency_check(input));

    auto result = split_dynamic_part(filter, input, output);

    for (const auto &moduleData: output)
    {
        assert(size_consistency_check(moduleData.md));
    }
    assert(!output.empty());
    assert(output.front().md.data.data == input.data.data);
    // TODO: handle input.suffix or warn + status?

    return result;
}

std::error_code event_data(State &state, Callbacks &callbacks, void *userContext, int ei,
                           const ModuleData *moduleDataList, unsigned moduleCount)
{
    state.processingFlags = 0;

    if (auto ec = begin_event(state, ei))
        return ec;

    // if out of range or not enabled for this event pass the data through
    if (static_cast<size_t>(ei) >= state.enabledForEvent.size() || !state.enabledForEvent[ei])
    {
        LOG_TRACE(
            "state=%p, splitting not enabled for ei=%d; invoking callbacks with non-split data",
            &state, ei);

        callbacks.eventData(userContext, state.outputCrateIndex, ei, moduleDataList, moduleCount);

        for (size_t mi = 0; mi < moduleCount; ++mi)
            ++state.counters.outputModules[ei][mi];
        ++state.counters.outputEvents[ei];

        return {};
    }

    if (moduleCount > state.splitFilters[ei].size())
        return make_error_code(ErrorCode::ModuleIndexOutOfRange);

    // clear the output module data
    state.splitModuleData.resize(moduleCount);
    std::for_each(state.splitModuleData.begin(), state.splitModuleData.end(),
                  [](auto &v) { v.clear(); });

    //  splits into state.splitModuleData
    for (unsigned mi = 0; mi < moduleCount; ++mi)
    {
        auto splitResult = split_module_data(state.splitFilters[ei][mi], moduleDataList[mi],
                                             state.splitModuleData[mi]);

        if (splitResult & ProcessingFlags::ModuleHeaderMismatch)
            ++state.counters.moduleHeaderMismatches[ei][mi];

        if (splitResult & ProcessingFlags::ModuleSizeExceedsBuffer)
            ++state.counters.moduleEventSizeExceedsBuffer[ei][mi];

        state.processingFlags |= splitResult;
        ++state.counters.inputModules[ei][mi];

        for (const auto &moduleData: state.splitModuleData[mi])
        {
            assert(size_consistency_check(moduleData.md));
        }
    }

    size_t maxEventCount = 0;

    for (unsigned mi = 0; mi < moduleCount; ++mi)
        maxEventCount = std::max(maxEventCount, state.splitModuleData[mi].size());

    state.dataSpans.resize(moduleCount);

    // Yield output events.
    for (size_t outIdx = 0; outIdx < maxEventCount; ++outIdx)
    {
        std::fill(std::begin(state.dataSpans), std::end(state.dataSpans), ModuleData{});
        for (unsigned mi = 0; mi < moduleCount; ++mi)
        {
            if (outIdx < state.splitModuleData[mi].size())
            {
                state.dataSpans[mi] = state.splitModuleData[mi][outIdx].md;
                ++state.counters.outputModules[ei][mi];
                assert(size_consistency_check(state.dataSpans[mi]));
            }
        }

        callbacks.eventData(userContext, state.outputCrateIndex, ei, state.dataSpans.data(),
                            state.dataSpans.size());
        ++state.counters.outputEvents[ei];
    }

    return {};
}

std::error_code LIBMVME_EXPORT make_error_code(ErrorCode error)
{
    return {static_cast<int>(error), theMultiEventSplitterErrorCategory};
}

template <typename Out> Out &format_counters_(Out &out, const Counters &counters)
{
    for (size_t ei = 0; ei < counters.inputEvents.size(); ++ei)
    {
        auto eventRatio = counters.outputEvents[ei] * 1.0 / counters.inputEvents[ei];
        out << fmt::format("* eventIndex={}, inputEvents={}, outputEvents={}, out/in={:.2f}\n", ei,
                           counters.inputEvents[ei], counters.outputEvents[ei], eventRatio);

        for (size_t mi = 0; mi < counters.inputModules[ei].size(); ++mi)
        {
            auto moduleRatio = counters.outputModules[ei][mi] * 1.0 / counters.inputModules[ei][mi];
            out << fmt::format(
                "  - moduleIndex={}, inputModuleCount={}, outputModuleCount={}, out/in={:.2f}\n",
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

template <typename Out> Out &format_counters_tabular_(Out &out, const Counters &counters)
{
    out << fmt::format("{: <12} {: >12} {: >12} {: >12}\n", "Type", "InputCount", "OutputCount",
                       "Out/In");

    for (size_t ei = 0; ei < counters.inputEvents.size(); ++ei)
    {
        auto eventRatio = counters.outputEvents[ei] * 1.0 / counters.inputEvents[ei];

        out << fmt::format("{: <12} {: >12L} {: >12L} {: >12.2Lf}\n", fmt::format("event{}", ei),
                           counters.inputEvents[ei], counters.outputEvents[ei], eventRatio);

        for (size_t mi = 0; mi < counters.inputModules[ei].size(); ++mi)
        {
            auto moduleRatio = counters.outputModules[ei][mi] * 1.0 / counters.inputModules[ei][mi];
            out << fmt::format("{: <12} {: >12L} {: >12L} {: >12.2Lf}\n",
                               fmt::format("module{}", mi), counters.inputModules[ei][mi],
                               counters.outputModules[ei][mi], moduleRatio);
        }
    }

    out << fmt::format("\n{: <22} {: >12}\n", "ErrorType", "ErrorCount");
    out << fmt::format("{: <22} {: >12}\n", "eventIndexOutOfRange", counters.eventIndexOutOfRange);
    out << fmt::format("{: <22} {: >12}\n", "moduleIndexOutOfRange",
                       counters.moduleIndexOutOfRange);

    out << "\nModule header mismatches:\n";
    for (size_t ei = 0; ei < counters.moduleHeaderMismatches.size(); ++ei)
    {
        const auto &mismatches = counters.moduleHeaderMismatches[ei];
        for (size_t mi = 0; mi < mismatches.size(); ++mi)
        {
            out << fmt::format("  {} {}\n", fmt::format("event{}, module{}", ei, mi),
                               mismatches[mi]);
        }
    }

    out << "\nModule size exceeds input event size:\n";
    for (size_t ei = 0; ei < counters.moduleEventSizeExceedsBuffer.size(); ++ei)
    {
        const auto &exceeds = counters.moduleEventSizeExceedsBuffer[ei];
        for (size_t mi = 0; mi < exceeds.size(); ++mi)
        {
            out << fmt::format("  {} {}\n", fmt::format("event{}, module{}", ei, mi), exceeds[mi]);
        }
    }

    return out;
}

std::ostream &format_counters_tabular(std::ostream &out, const Counters &counters)
{
    return format_counters_tabular_(out, counters);
}

} // namespace mesytec::mvme::multi_event_splitter
