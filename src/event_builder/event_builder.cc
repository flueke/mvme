#include "event_builder/event_builder.h"

namespace mvme
{
namespace event_builder
{

using namespace a2::data_filter;

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

static const u32 TimestampMax = 0x3fffffffu; // 30 bits
static const u32 TimestampHalf = TimestampMax >> 1;

WindowMatchResult timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow)
{
    s64 diff = static_cast<s64>(tsMain) - static_cast<s64>(tsModule);

    if (std::abs(diff) > TimestampHalf)
    {
        if (diff < 0)
            diff += TimestampMax;
        else
            diff -= TimestampMax;
    }

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
    // Reverse mapping back from calculated linear modules indexes to (crateIndex, moduleIndex)
    // indexes: event, linear module index -> pair(crateIndex, moduleIndex)
    //std::vector<std::unordered_map<size_t, std::pair<int, unsigned>>> reverseModuleIndexTable_;
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
    std::vector<std::vector<size_t>> moduleDiscardedEvents_;
    // indexes: event, linear module
    std::vector<std::vector<size_t>> moduleEmptyEvents_;
    // indexes: event, linear module
    std::vector<std::vector<size_t>> moduleInvScoreSums_;

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
        auto &discardedEvents = moduleDiscardedEvents_.at(eventIndex);
        auto &invScores = moduleInvScoreSums_.at(eventIndex);

        // Check if there is at least one event from the main module
        if (mainBuffer.empty())
            return 0u;

        eventAssembly_.resize(moduleCount);

        size_t result = 0u;
        const auto &setup = setups_.at(eventIndex);

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
                            eventBuffers.at(moduleIndex).pop_front();
                            ++discardedEvents.at(moduleIndex);
                            break;

                        case WindowMatch::in_window:
                            eventAssembly_[moduleIndex] = module_data_from_event_storage(moduleEvent);
                            eventInvScore += matchResult.invscore;
                            invScores.at(moduleIndex) += matchResult.invscore;
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
            // Unless virtual events are created the solution with multiple
            // calls to eventData() won't work.  The drawback of having extra
            // events is that they introduce another analysis context operators
            // can run in which is separate from the oirignal event we want to
            // report stats for.
            callbacks.eventData(userContext_, eventIndex, eventAssembly_.data(), moduleCount);
            ++result;

            // Now, after, the callback, pop the consumed module events off the deques.
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

        // Clear all module event buffers if flush was requested.
        if (flush)
        {
            std::for_each(std::begin(eventBuffers), std::end(eventBuffers),
                          [] (auto &eb) { eb.clear(); });

            assert(std::all_of(std::begin(eventBuffers), std::end(eventBuffers),
                               [] (const auto &eb) { return eb.empty(); }));
        }

        return result;
    }

    EventBuilder::EventCounters getCounters(int eventIndex) const
    {
        EventCounters ret = {};
        ret.discardedEvents = moduleDiscardedEvents_.at(eventIndex);
        ret.emptyEvents = moduleEmptyEvents_.at(eventIndex);
        ret.invScoreSums = moduleInvScoreSums_.at(eventIndex);
        return ret;
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
    d->moduleDiscardedEvents_.resize(eventCount);
    d->moduleEmptyEvents_.resize(eventCount);
    d->moduleInvScoreSums_.resize(eventCount);

    for (size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
    {
        const auto &eventSetup = d->setups_.at(eventIndex);

        if (!eventSetup.enabled)
            continue;

        auto &eventTable = d->linearModuleIndexTable_.at(eventIndex);
        auto &timestampExtractors = d->moduleTimestampExtractors_.at(eventIndex);
        auto &matchWindows = d->moduleMatchWindows_.at(eventIndex);
        auto &eventBuffers = d->moduleEventBuffers_.at(eventIndex);
        auto &discardedEvents = d->moduleDiscardedEvents_.at(eventIndex);
        auto &emptyEvents = d->moduleEmptyEvents_.at(eventIndex);
        auto &invScores = d->moduleInvScoreSums_.at(eventIndex);
        unsigned linearModuleIndex = 0;

        for (size_t crateIndex = 0; crateIndex < eventSetup.crateSetups.size(); ++crateIndex)
        {
            const auto &crateSetup = eventSetup.crateSetups.at(crateIndex);

            assert(crateSetup.moduleTimestampExtractors.size() == crateSetup.moduleMatchWindows.size());

            const size_t moduleCount = crateSetup.moduleTimestampExtractors.size();

            for (size_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
            {
                auto key = std::make_pair(crateIndex, moduleIndex);
                eventTable[key] = linearModuleIndex;
                ++linearModuleIndex;

                timestampExtractors.push_back(crateSetup.moduleTimestampExtractors[moduleIndex]);
                matchWindows.push_back(crateSetup.moduleMatchWindows[moduleIndex]);
            }

            eventBuffers.resize(moduleCount);
            discardedEvents.resize(moduleCount);
            emptyEvents.resize(moduleCount);
            invScores.resize(moduleCount);
        }

        size_t mainModuleLinearIndex = d->getLinearModuleIndex(
            eventSetup.mainModule.first, // crateIndex
            eventIndex,
            eventSetup.mainModule.second); // moduleIndex

        d->mainModuleLinearIndexes_[eventIndex] = mainModuleLinearIndex;
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
    auto &emptyEvents = d->moduleEmptyEvents_.at(eventIndex);

    for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        auto moduleData = moduleDataList[moduleIndex];

        auto &prefix = moduleData.prefix;
        auto &dynamic = moduleData.dynamic;
        auto &suffix = moduleData.suffix;

        // The readout parser can yield zero length data if a module is read
        // out using a block transfer but the module has not converted any
        // events at all. In this case it will immediately raise BERR on the
        // VME bus. This is different than the case where the module got a
        // trigger but no channel was within the thresholds. Then we do get an
        // event consisting of only the header and footer (containing the
        // timestamp).
        // The zero length events need to be skipped as there is no timestamp
        // information contained within and the builder code assumes non-zero
        // data for module events.
        if (dynamic.size == 0)
        {
            ++emptyEvents.at(moduleIndex);
            continue;
        }

        const auto linearModuleIndex = d->getLinearModuleIndex(crateIndex, eventIndex, moduleIndex);
        u32 timestamp = timestampExtractors.at(linearModuleIndex)(dynamic.data, dynamic.size);

        assert(timestamp <= TimestampMax);

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

        for (const auto &moduleBuffers: d->moduleEventBuffers_)
        {
            for (const auto &moduleBuffer: moduleBuffers)
            {
                if (!moduleBuffer.empty())
                    return true;
            }
        }

        return false;
    };

    UniqueLock guard(d->mutex_);
    return d->cv_.wait_for(guard, maxWait, predicate);
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

EventBuilder::EventCounters EventBuilder::getCounters(int eventIndex) const
{
    UniqueLock guard(d->mutex_);
    return d->getCounters(eventIndex);
}

std::vector<EventBuilder::EventCounters> EventBuilder::getCounters() const
{
    UniqueLock guard(d->mutex_);

    std::vector<EventCounters> ret;
    ret.reserve(d->moduleDiscardedEvents_.size());

    for (size_t ei=0; ei<d->moduleDiscardedEvents_.size(); ++ei)
        ret.emplace_back(d->getCounters(ei));

    return ret;
}

} // end namespace event_builder
} // end namespace mvme
