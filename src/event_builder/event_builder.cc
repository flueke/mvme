#include "event_builder/event_builder.h"

#include <boost/circular_buffer.hpp>
#include <boost/dynamic_bitset.hpp>
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

// FIXME: This way of buffering module data is not ideal at all: 3 potential
// allocations per event and module is a lot.
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

struct EventBuilder::Private
{
    void *userContext_ = nullptr;

    std::vector<EventSetup> setups_;

    TicketMutex mutex_;
    std::condition_variable_any cv_;

    // copies of systemEvents
    std::deque<SystemEventStorage> systemEvents_;

    // indexes: event, pair(crateIndex, moduleIndex) -> linear module index
    std::vector<std::unordered_map<std::pair<int, unsigned>, size_t, PairHash>> linearModuleIndexTable_;
    // Linear module index of the main module for each event
    // indexes: event, linear module
    std::vector<size_t> mainModuleLinearIndexes_;
    // Holds copies of module event data and the extracted event timestamp.
    // indexes: event, linear module, buffered event
    std::vector<std::vector<std::deque<ModuleEventStorage>>> moduleEventBuffers_;
    // indexes: event, linear module
    std::vector<std::vector<timestamp_extractor>> moduleTimestampExtractors_;
    // indexes: event, linear module
    std::vector<std::vector<std::pair<s32, s32>>> moduleMatchWindows_;
    // indexes: event, linear module
    std::vector<std::vector<u32>> prevModuleTimestamps_;

    std::vector<ModuleData> eventAssembly_;

    size_t getLinearModuleIndex(int crateIndex, int eventIndex, unsigned moduleIndex) const
    {
        assert(0 <= eventIndex && static_cast<size_t>(eventIndex) < linearModuleIndexTable_.size());
        const auto &eventTable = linearModuleIndexTable_.at(eventIndex);
        const auto key = std::make_pair(crateIndex, moduleIndex);
        assert(eventTable.find(key) != eventTable.end());
        return eventTable.at(key);
    }

    size_t buildEvents(int eventIndex, Callbacks &callbacks, bool flush)
    {
        auto &eventBuffers = moduleEventBuffers_.at(eventIndex);
        const auto &matchWindows = moduleMatchWindows_.at(eventIndex);
        assert(eventBuffers.size() == matchWindows.size());
        const size_t moduleCount = eventBuffers.size();
        auto mainModuleIndex = mainModuleLinearIndexes_.at(eventIndex);
        assert(mainModuleIndex < moduleCount);
        const auto &mainBuffer = eventBuffers.at(mainModuleIndex);
        auto &prevTimestamps = prevModuleTimestamps_.at(eventIndex);

        // Check if there is at least one event from the main module
        if (mainBuffer.empty())
            return 0u;

        eventAssembly_.resize(moduleCount);

        size_t result = 0u;
        const auto &setup = setups_.at(eventIndex);
        boost::dynamic_bitset<> overflows;
        overflows.resize(moduleCount);
        static const u32 TimestampMax = 0x3fffffffu;
        // XXX: leftoff here

        while (!mainBuffer.empty()
               && (flush || mainBuffer.size() >= setup.minMainModuleEvents))
        {
            u32 mainModuleTimestamp = eventBuffers.at(mainModuleIndex).front().timestamp;
            std::fill(eventAssembly_.begin(), eventAssembly_.end(), ModuleData{});
            u32 eventInvScore = 0u;

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                if (eventBuffers.at(moduleIndex).empty())
                    continue;

                auto &matchWindow = matchWindows[moduleIndex];
                bool done = false;

                while (!done && !eventBuffers.at(moduleIndex).empty())
                {
                    auto &moduleEvent = eventBuffers.at(moduleIndex).front();
                    auto matchResult = timestamp_match(mainModuleTimestamp, moduleEvent.timestamp, matchWindow);

                    switch (matchResult.match)
                    {
                        case WindowMatch::too_old:
                            prevTimestamps[moduleIndex] = moduleEvent.timestamp;
                            eventBuffers.at(moduleIndex).pop_front();
                            break;

                        case WindowMatch::in_window:
                            eventAssembly_[moduleIndex] = module_data_from_event_storage(moduleEvent);
                            eventInvScore += matchResult.invscore;
                            done = true;
                            break;

                        case WindowMatch::too_new:
                            done = true;
                            break;
                    }
                }
            }

            // TODO: pass eventInvScore as a quality measure to the analysis
            // could either add an EventMeta object and udpate all the callbacks in the chain
            // or could create more virtual events and modules and pass the data in via separate calls
            // to eventData() using the indexes of the virtual objects.
            callbacks.eventData(userContext_, eventIndex, eventAssembly_.data(), moduleCount);
            ++result;

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto &moduleData = eventAssembly_[moduleIndex];

                if (moduleData.prefix.data || moduleData.dynamic.data || moduleData.suffix.data)
                {
                    assert(!eventBuffers.at(moduleIndex).empty());
                    eventBuffers.at(moduleIndex).pop_front();
                }
            }
        }

        assert(mainBuffer.size() < setup.minMainModuleEvents);

        return result;
    }
};

EventBuilder::EventBuilder(const std::vector<EventSetup> &setups, void *userContext)
    : d(std::make_unique<Private>())
{
    d->userContext_ = userContext;
    d->setups_ = setups;

    const size_t eventCount = d->setups_.size();

    d->linearModuleIndexTable_.resize(eventCount);
    d->mainModuleLinearIndexes_.resize(eventCount);
    d->moduleEventBuffers_.resize(eventCount);
    d->moduleTimestampExtractors_.resize(eventCount);
    d->moduleMatchWindows_.resize(eventCount);
    d->prevModuleTimestamps_.resize(eventCount);

    for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        const auto &eventSetup = d->setups_.at(eventIndex);

        if (!eventSetup.enabled)
            continue;

        auto &eventTable = d->linearModuleIndexTable_.at(eventIndex);
        auto &timestampExtractors = d->moduleTimestampExtractors_.at(eventIndex);
        auto &matchWindows = d->moduleMatchWindows_.at(eventIndex);
        auto &eventBuffers = d->moduleEventBuffers_.at(eventIndex);
        auto &prevTimestamps = d->prevModuleTimestamps_.at(eventIndex);
        unsigned linearModuleIndex = 0;

        for (size_t crateIndex = 0; crateIndex < eventSetup.crateSetups.size(); ++crateIndex)
        {
            const auto &crateSetup = eventSetup.crateSetups.at(crateIndex);

            assert(crateSetup.moduleTimestampExtractors.size() == crateSetup.moduleMatchWindows.size());

            const size_t moduleCount = crateSetup.moduleTimestampExtractors.size();

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto key = std::make_pair(crateIndex, moduleIndex);
                eventTable[key] = linearModuleIndex++;
                timestampExtractors.push_back(crateSetup.moduleTimestampExtractors[moduleIndex]);
                matchWindows.push_back(crateSetup.moduleMatchWindows[moduleIndex]);
                eventBuffers.push_back({});
            }

            prevTimestamps.resize(moduleCount);
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

bool EventBuilder::isEnabledFor(int eventIndex) const
{
    if (0 <= eventIndex && static_cast<size_t>(eventIndex) < d->setups_.size())
        return d->setups_[eventIndex].enabled;
    return false;
}

bool EventBuilder::isEnabledForAnyEvent() const
{
    return std::any_of(d->setups_.begin(), d->setups_.end(),
                       [] (const EventSetup &setup) { return setup.enabled; });
}

void EventBuilder::recordEventData(int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
    // lock, then copy the data to an internal buffer
    UniqueLock guard(d->mutex_);

    assert(0 <= crateIndex);
    assert(0 <= eventIndex);
    auto &moduleEventBuffers = d->moduleEventBuffers_.at(eventIndex);
    auto &timestampExtractors = d->moduleTimestampExtractors_.at(eventIndex);

    for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto moduleData = moduleDataList[moduleIndex];

        auto &prefix = moduleData.prefix;
        auto &dynamic = moduleData.dynamic;
        auto &suffix = moduleData.suffix;

        if (dynamic.size == 0) // FIXME: why do we get 0 sized data from the readout parser?
            continue;

        auto linearModuleIndex = d->getLinearModuleIndex(crateIndex, eventIndex, moduleIndex);

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

void EventBuilder::recordSystemEvent(int crateIndex, const u32 *header, u32 size)
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
                    [] (const auto &moduleBuffer)
                    {
                        return !moduleBuffer.empty();
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

WindowMatchResult timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow)
{
    // FIXME: proper overflow handling
    //static const s64 TimestampMax = 0xFFFFFFFF;

    s64 diff = static_cast<s64>(tsMain) - static_cast<s64>(tsModule);

    if (diff >= 0)
    {
        // tsModule is before tsMain
        if (diff > -matchWindow.first)
            return { WindowMatch::too_old, static_cast<u32>(std::abs(diff)) };
    }
    else
    {
        // tsModule is after tsMain
        if (-diff > matchWindow.second)
            return { WindowMatch::too_new, static_cast<u32>(std::abs(diff)) };
    }

    return { WindowMatch::in_window, static_cast<u32>(std::abs(diff)) };
}

size_t EventBuilder::buildEvents(Callbacks callbacks, bool flush)
{
    UniqueLock guard(d->mutex_);

    // system events
    while (!d->systemEvents_.empty())
    {
        auto &ses = d->systemEvents_.back();
        // FIXME: crateIndex (analysis needs to know)
        callbacks.systemEvent(d->userContext_, ses.data.data(), ses.data.size());
        d->systemEvents_.pop_front();
    }

    assert(d->systemEvents_.empty());

    // readout event building
    const size_t eventCount = d->setups_.size();
    size_t result = 0u;

    for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        if (d->setups_[eventIndex].enabled)
            result += d->buildEvents(eventIndex, callbacks, flush);
    }

    return result;
}

} // end namespace event_builder
} // end namespace mvme
