#include "multi_event_splitter.h"

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

    for (const auto &moduleStrings: splitFilterStrings)
    {
        std::vector<State::FilterWithCache> moduleFilters;

        for (const auto &moduleString: moduleStrings)
        {
            State::FilterWithCache fc;

            fc.filter = a2::data_filter::make_filter(moduleString);
            fc.cache = a2::data_filter::make_cache_entry(fc.filter, 'S');

            moduleFilters.emplace_back(fc);
        }

        result.splitFilters.emplace_back(moduleFilters);
    }

    // Allocate space for the module data spans for each event
    for (const auto &filters: result.splitFilters)
    {
        std::vector<State::ModuleDataSpans> spans(filters.size(), State::ModuleDataSpans{});
        result.dataSpans.emplace_back(spans);
    }

    // For each event determine if splitting should be enabled. This is the
    // case if any of the events modules has a non-zero header filter.
    for (const auto &filters: result.splitFilters)
    {
        bool hasNonZeroFilter = std::any_of(
            filters.begin(), filters.end(),
            [] (const State::FilterWithCache &fc)
            {
                return fc.filter.matchMask != 0;
            });

        result.enabledForEvent.push_back(hasNonZeroFilter);
    }

    assert(result.enabledForEvent.size() == result.splitFilters.size());

    // Find the longest of the filter string vectors. This is the maximum
    // number of modules across all events.
    auto it = std::max_element(
        splitFilterStrings.begin(), splitFilterStrings.end(),
        [] (const auto &vec1, const auto &vec2)
        {
            return vec1.size() < vec2.size();
        });

    size_t maxModuleCount = 0;

    if (it != splitFilterStrings.end())
        maxModuleCount = it->size();

    result.moduleFilterMatches.resize(maxModuleCount);

    assert(result.splitFilters.size() == result.dataSpans.size());

    return result;
}

std::error_code begin_event(State &state, int ei)
{
    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei >= static_cast<int>(state.dataSpans.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans[ei];

    std::fill(spans.begin(), spans.end(), State::ModuleDataSpans{});

    return {};
}

// The module_(prefix|dynamic|suffix) functions record the data pointer and
// size in the splitters state structure for later use in the end_event
// function.

std::error_code module_prefix(State &state, int ei, int mi, const u32 *data, u32 size)
{
    if (ei >= static_cast<int>(state.dataSpans.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans[ei];

    if (mi >= static_cast<int>(spans.size()))
        return make_error_code(ErrorCode::ModuleIndexOutOfRange);

    spans[mi].prefixSpan = { data, data + size };

    LOG_TRACE("state=%p, ei=%d, mi=%d, data=%p, dataSize=%u",
              &state, ei, mi, data, size);

    return {};
}

std::error_code module_data(State &state, int ei, int mi, const u32 *data, u32 size)
{
    if (ei >= static_cast<int>(state.dataSpans.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans[ei];

    if (mi >= static_cast<int>(spans.size()))
        return make_error_code(ErrorCode::ModuleIndexOutOfRange);

    spans[mi].dynamicSpan = { data, data + size };

    LOG_TRACE("state=%p, ei=%d, mi=%d, data=%p, dataSize=%u",
              &state, ei, mi, data, size);

    return {};
}

std::error_code module_suffix(State &state, int ei, int mi, const u32 *data, u32 size)
{
    if (ei >= static_cast<int>(state.dataSpans.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    auto &spans = state.dataSpans[ei];

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
    LOG_TRACE("state=%p, ei=%d", &state, ei);

    if (ei >= static_cast<int>(state.dataSpans.size()))
        return make_error_code(ErrorCode::EventIndexOutOfRange);

    assert(state.splitFilters.size() == state.dataSpans.size());

    auto &moduleFilters = state.splitFilters[ei];
    auto &moduleSpans = state.dataSpans[ei];
    const size_t moduleCount = moduleSpans.size();

    LOG_TRACE("state=%p, ei=%d, moduleCount=%lu", &state, ei, moduleCount);

    assert(moduleFilters.size() == moduleSpans.size());
    assert(state.moduleFilterMatches.size() >= moduleCount);

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

    // Split the data of each of the modules for this event using the
    // data_filter for header matching and size extraction.
    // Terminate if the data of all modules has been used up or none of the
    // modules have a header filter match.
    while (true)
    {
        // clear every bit to 0
        state.moduleFilterMatches.reset();

        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            auto &dynamicSpan = moduleSpans[mi].dynamicSpan;

            if (words_in_span(dynamicSpan))
            {
                bool hasMatch = a2::data_filter::matches(
                    moduleFilters[mi].filter, *dynamicSpan.begin);

                state.moduleFilterMatches[mi] = hasMatch;

                LOG_TRACE("state=%p, ei=%d, mi=%lu, checked header '0x%08x', match=%s",
                          &state, ei, mi, *dynamicSpan.begin, hasMatch ? "true" : "false")
            }
        }

        // Termination condition: none of the modules have any more dynamic
        // data left or the header filter did not match for any of them.
        if (state.moduleFilterMatches.none())
            break;

        LOG_TRACE("state=%p, callbacks.beginEvent(%d)", &state, ei);
        callbacks.beginEvent(ei);

        for (size_t mi = 0; mi < moduleCount; ++mi)
        {
            if (state.moduleFilterMatches[mi])
            {
                auto &spans = moduleSpans[mi];

                // If there are no more words in the span then the bit
                // indicating a match should not have been set.
                assert(words_in_span(spans.dynamicSpan));

                // Add one to the extracted module event size to account for
                // the header word itself (the extracted size is the number of
                // words following the header word).
                u32 moduleEventSize = 1 + a2::data_filter::extract(
                    moduleFilters[mi].cache, *spans.dynamicSpan.begin);

                LOG_TRACE("state=%p, ei=%d, mi=%lu, words in dynamicSpan=%lu, moduleEventSize=%u, header=0x%08x",
                          &state, ei, mi, words_in_span(spans.dynamicSpan), moduleEventSize,
                          *spans.dynamicSpan.begin);

                if (moduleEventSize > words_in_span(spans.dynamicSpan))
                {
                    // The extracted event size exceeds the amount of data left
                    // in the dynamic span. Move the span begin pointer forward
                    // so that the span has size 0 and the module filter test
                    // above will fail on the next iteration.
                    spans.dynamicSpan.begin = spans.dynamicSpan.end;
                    continue;
                }

                // Use the same prefix data each time we yield module data.
                if (auto size = words_in_span(spans.prefixSpan))
                {
                    LOG_TRACE("state=%p, callbacks.modulePrefx(ei=%d, mi=%lu, data=%p, size=%lu",
                              &state, ei, mi, spans.prefixSpan.begin, size);

                    callbacks.modulePrefix(ei, mi, spans.prefixSpan.begin, size);
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

                // Use the same suffix data each time we yield module data.
                if (auto size = words_in_span(spans.suffixSpan))
                {
                    LOG_TRACE("state=%p, callbacks.moduleSuffix(ei=%d, mi=%lu, data=%p, size=%lu",
                              &state, ei, mi, spans.suffixSpan.begin, size);

                    callbacks.moduleSuffix(ei, mi, spans.suffixSpan.begin, size);
                }
            }
        }

        LOG_TRACE("state=%p, callbacks.endEvent(%d)", &state, ei);
        callbacks.endEvent(ei);
    }

    return {};
}

std::error_code LIBMVME_EXPORT make_error_code(ErrorCode error)
{
    return { static_cast<int>(error), theMultiEventSplitterErrorCategory };
}

} // end namespace multi_event_splitter
} // end namespace mvme
