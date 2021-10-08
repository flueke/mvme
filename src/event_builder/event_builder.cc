#include "event_builder/event_builder.h"

#include <boost/circular_buffer.hpp>
#include <iostream>

namespace mvme
{
namespace event_builder
{

using namespace a2::data_filter;
using std::cerr;
using std::endl;

IndexedTimestampFilterExtractor::IndexedTimestampFilterExtractor(const DataFilter &filter, s32 wordIndex, char matchChar)
    : filter_(filter)
    , filterCache_(make_cache_entry(filter_, matchChar))
    , index_(wordIndex)
{
}

u32 IndexedTimestampFilterExtractor::operator()(const u32 *data, size_t size)
{
    if (index_ < 0)
    {
        ssize_t abs = size + index_;

        if (0 <= abs && static_cast<size_t>(abs) < size && matches(filter_, data[abs]))
            return extract(filterCache_, data[abs]);
    }
    else if (static_cast<size_t>(index_) < size && matches(filter_, data[index_]))
    {
        return extract(filterCache_, data[index_]);
    }

    return 0u;
}

TimestampFilterExtractor::TimestampFilterExtractor(const DataFilter &filter, char matchChar)
    : filter_(filter)
    , filterCache_(make_cache_entry(filter_, matchChar))
{
}

u32 TimestampFilterExtractor::operator()(const u32 *data, size_t size)
{
    for (const u32 *valuep = data; valuep < data + size; ++valuep)
    {
        if (matches(filter_, *valuep))
            return extract(filterCache_, *valuep);
    }

    return 0u;
}

struct SystemEventStorage
{
    int crateIndex;
    std::vector<u32> data;
};

struct ModuleEventStorage
{
    u32 timestamp;
    std::vector<u32> prefix;
    std::vector<u32> dynamic;
    std::vector<u32> suffix;
};

using mesytec::mvlc::TicketMutex;
using mesytec::mvlc::UniqueLock;
using mesytec::mvlc::readout_parser::PairHash;


struct EventBuilder::Private
{
    void *userContext_ = nullptr;

    std::vector<EventSetup> setup_;

    TicketMutex mutex_;
    std::condition_variable_any cv_;

    // copies of systemEvents
    std::deque<SystemEventStorage> systemEvents_;

    // Holds copies of module event data and the extracted event timestamp.
    // indexes: event, linear module, buffered event
    std::vector<std::vector<std::deque<ModuleEventStorage>>> moduleEventBuffers_;
    // indexes: event, linear module
    std::vector<std::vector<timestamp_extractor>> moduleTimestampExtractors_;
    // indexes: event, linear module
    std::vector<std::vector<std::pair<s32, s32>>> moduleMatchWindows_;
    // indexes: event, pair(crateIndex, moduleIndex)
    std::vector<std::unordered_map<std::pair<int, unsigned>, size_t, PairHash>> linearModuleIndexTable_;
    // indexes: event, linear module
    std::vector<size_t> mainModuleLinearIndexes_;

    std::vector<ModuleData> eventAssembly_;

    size_t getLinearModuleIndex(int crateIndex, int eventIndex, unsigned moduleIndex) const
    {
        assert(0 <= eventIndex && static_cast<size_t>(eventIndex) < linearModuleIndexTable_.size());
        const auto &eventTable = linearModuleIndexTable_.at(eventIndex);
        const auto key = std::make_pair(crateIndex, moduleIndex);
        assert(eventTable.find(key) != eventTable.end());
        return eventTable.at(key);
    }
};

EventBuilder::EventBuilder(const std::vector<EventSetup> &setup, void *userContext)
    : d(std::make_unique<Private>())
{
    d->userContext_ = userContext;
    d->setup_ = setup;

    const size_t eventCount = d->setup_.size();

    d->moduleEventBuffers_.resize(eventCount);
    d->moduleTimestampExtractors_.resize(eventCount);
    d->moduleMatchWindows_.resize(eventCount);
    d->linearModuleIndexTable_.resize(eventCount);
    d->mainModuleLinearIndexes_.resize(eventCount);

    for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        const auto &eventSetup = d->setup_.at(eventIndex);
        auto &eventTable = d->linearModuleIndexTable_.at(eventIndex);
        auto &timestampExtractors = d->moduleTimestampExtractors_.at(eventIndex);
        auto &matchWindows = d->moduleMatchWindows_.at(eventIndex);
        auto &eventBuffers = d->moduleEventBuffers_.at(eventIndex);
        unsigned linearModuleIndex = 0;

        for (size_t crateIndex = 0; crateIndex < eventSetup.crateSetups.size(); ++crateIndex)
        {
            const auto &crateSetup = eventSetup.crateSetups.at(crateIndex);

            assert(crateSetup.moduleTimestampExtractors.size() == crateSetup.moduleMatchWindows.size());

            for (size_t moduleIndex = 0; moduleIndex < crateSetup.moduleTimestampExtractors.size(); ++moduleIndex)
            {
                auto key = std::make_pair(crateIndex, moduleIndex);
                eventTable[key] = linearModuleIndex++;
                timestampExtractors.push_back(crateSetup.moduleTimestampExtractors[moduleIndex]);
                matchWindows.push_back(crateSetup.moduleMatchWindows[moduleIndex]);
                eventBuffers.push_back({});
            }
        }

        size_t mainModuleLinearIndex = d->getLinearModuleIndex(
            eventSetup.mainModule.first, // crateIndex
            eventIndex,
            eventSetup.mainModule.second); // moduleIndex

        d->mainModuleLinearIndexes_[eventIndex] = mainModuleLinearIndex;

        //cerr << "event=" << eventIndex << ", mainModuleLinearIndex=" << mainModuleLinearIndex << endl;
    }
}

EventBuilder::~EventBuilder()
{
}

EventBuilder::EventBuilder(EventBuilder &&o)
{
    d = std::move(o.d);
}

EventBuilder &EventBuilder::operator=(EventBuilder &&o)
{
    d = std::move(o.d);
    return *this;
}

void EventBuilder::pushEventData(int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
    // lock, then copy the data to an internal buffer
    UniqueLock guard(d->mutex_);

    auto &moduleEventBuffers = d->moduleEventBuffers_.at(eventIndex);
    auto &timestampExtractors = d->moduleTimestampExtractors_.at(eventIndex);

    for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto moduleData = moduleDataList[moduleIndex];
        auto linearModuleIndex = d->getLinearModuleIndex(crateIndex, eventIndex, moduleIndex);

        auto &prefix = moduleData.prefix;
        auto &dynamic = moduleData.dynamic;
        auto &suffix = moduleData.suffix;

        u32 timestamp = timestampExtractors.at(linearModuleIndex)(dynamic.data, dynamic.size);

        ModuleEventStorage eventStorage =
        {
            timestamp,
            { prefix.data, prefix.data + prefix.size },
            { dynamic.data, dynamic.data + dynamic.size },
            { suffix.data, suffix.data + suffix.size },
        };

        moduleEventBuffers.at(linearModuleIndex).emplace_back(eventStorage);
    }

    guard.unlock();
    d->cv_.notify_one();
}

void EventBuilder::pushSystemEvent(int crateIndex, const u32 *header, u32 size)
{
    // lock, then copy the data to an internal buffer
    SystemEventStorage ses = { crateIndex, { header, header + size } };

    UniqueLock guard(d->mutex_);
    d->systemEvents_.emplace_back(ses);

    guard.unlock();
    d->cv_.notify_one();
}

bool EventBuilder::waitForData(const std::chrono::milliseconds &maxWait)
{
    auto predicate = [this] ()
    {
        if (!d->systemEvents_.empty())
            return true;

#if 0
        return std::any_of(
            d->moduleEventBuffers_.begin(), d->moduleEventBuffers_.end(),
            [] (const auto &moduleBuffers)
            {
                return std::any_of(
                    moduleBuffers.begin(), moduleBuffers.end(),
                    [] (const auto &moduleQueue)
                    {
                        return !moduleQueue.empty();
                    });
            });
#else
        for (const auto &moduleBuffers: d->moduleEventBuffers_)
        {
            for (const auto &moduleBuffer: moduleBuffers)
            {
                if (!moduleBuffer.empty())
                    return true;
            }
        }
#endif

        return false;
    };

    UniqueLock guard(d->mutex_);
    return d->cv_.wait_for(guard, maxWait, predicate);
}

ModuleData module_data_from_event_storage(const ModuleEventStorage &input)
{
    auto result = ModuleData
    {
        { input.prefix.data(), static_cast<u32>(input.prefix.size()) },
        { input.dynamic.data(), static_cast<u32>(input.dynamic.size()) },
        { input.suffix.data(), static_cast<u32>(input.suffix.size()) },
    };

    return result;
}

WindowMatchResult timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow)
{
    // FIXME: proper overflow handling
    //static const s64 TimestampMax = 0xFFFFFFFF;

    s64 diff = static_cast<s64>(tsMain) - static_cast<s64>(tsModule);

    if (diff >= 0)
    {
        // tsModule is before tsMain
        if (diff > -matchWindow.first)
            return WindowMatchResult::too_old;
    }
    else
    {
        // tsModule is after tsMain
        if (-diff > matchWindow.second)
            return WindowMatchResult::too_new;
    }

    return WindowMatchResult::in_window;
}

size_t EventBuilder::buildEvents(Callbacks callbacks)
{
    UniqueLock guard(d->mutex_);

    // system events
    while (!d->systemEvents_.empty())
    {
        auto &ses = d->systemEvents_.back();
        // FIXME: crateIndex (analysis needs to know)
        callbacks.systemEvent(d->userContext_, ses.data.data(), ses.data.size());
        d->systemEvents_.pop_back();
    }

    assert(d->systemEvents_.empty());

    // readout event building
    const size_t eventCount = d->moduleEventBuffers_.size();

    // TODO: split this into a function taking the event index.
    for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        auto &eventBuffers = d->moduleEventBuffers_.at(eventIndex);
        auto &matchWindows = d->moduleMatchWindows_.at(eventIndex);
        assert(eventBuffers.size() == matchWindows.size());
        const size_t moduleCount = eventBuffers.size();
        auto mainModuleIndex = d->mainModuleLinearIndexes_.at(eventIndex);
        assert(mainModuleIndex < moduleCount);

        // Check if there is at least one event from the main module
        if (eventBuffers[mainModuleIndex].empty())
            continue;

        d->eventAssembly_.resize(moduleCount);

        while (!eventBuffers[mainModuleIndex].empty())
        {
            auto mainModuleTimestamp = eventBuffers[mainModuleIndex].front().timestamp;
            std::fill(d->eventAssembly_.begin(), d->eventAssembly_.end(), ModuleData{});
            bool skipToNextEventIndex = false;

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                // TODO: leave the loop here and try again later unless there are >= N main module events buffered.
                // In this case assume that the missing modules data won't
                // arrive anymore so we yield an event with missing modules.

                // Check if there is data for the current module.
                if (eventBuffers[moduleIndex].empty())
                {
                // FIXME: do use the max buffered check
                #if 1
                    d->eventAssembly_[moduleIndex] = {};
                    continue; // to next module
                #else
                    // No data for this module. We could now either yield the
                    // event with this module set to a null event or wait for
                    // more data and later on attempt to match this event
                    // again. Decide based on the number of buffered main
                    // module events.
                    if (eventBuffers[mainModuleIndex].size() > d->setup_[eventIndex].maxBufferedMainModuleEvents)
                    {
                        d->eventAssembly_[moduleIndex] = {};
                        continue; // to next module
                    }
                    else
                    {
                        // break out of the module loop
                        skipToNextEventIndex = true;
                        break;
                    }
                #endif
                }

                auto &matchWindow = matchWindows[moduleIndex];
                bool done = false;

                while (!done && !eventBuffers[moduleIndex].empty())
                {
                    auto &moduleEvent = eventBuffers[moduleIndex].front();

                    switch (timestamp_match(mainModuleTimestamp, moduleEvent.timestamp, matchWindow))
                    {
                        case WindowMatchResult::too_old:
                            eventBuffers[moduleIndex].pop_front();
                            break;

                        case WindowMatchResult::in_window:
                            d->eventAssembly_[moduleIndex] = module_data_from_event_storage(moduleEvent);
                            done = true;
                            break;

                        case WindowMatchResult::too_new:
                            d->eventAssembly_[moduleIndex] = {};
                            done = true;
                            break;
                    }
                }
            }

            // skip to the next event index
            if (skipToNextEventIndex)
                break;

            callbacks.eventData(d->userContext_, eventIndex, d->eventAssembly_.data(), moduleCount);

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto &moduleData = d->eventAssembly_[moduleIndex];

                if (moduleData.prefix.data || moduleData.dynamic.data || moduleData.suffix.data)
                {
                    assert(!eventBuffers[moduleIndex].empty());
                    eventBuffers[moduleIndex].pop_front();
                }
            }
        }
    }

    return 0u;
}

} // end namespace event_builder
} // end namespace mvme
