#ifndef __MVME_EVENT_BUILDER_H__
#define __MVME_EVENT_BUILDER_H__

#include <functional>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "analysis/a2/a2_data_filter.h"

namespace mvme
{
namespace event_builder
{

using DataFilter = a2::data_filter::DataFilter;
using ModuleData = mesytec::mvlc::readout_parser::ModuleData;
using Callbacks = mesytec::mvlc::readout_parser::ReadoutParserCallbacks;
using timestamp_extractor = std::function<u32 (const u32 *data, size_t size)>;

struct IndexedTimestampFilterExtractor
{
    public:
        IndexedTimestampFilterExtractor(const DataFilter &filter, s32 wordIndex, char matchChar = 'D');

        u32 operator()(const u32 *data, size_t size);

    private:
        DataFilter filter_;
        a2::data_filter::CacheEntry filterCache_;
        s32 index_;
};

struct TimestampFilterExtractor
{
    public:
        TimestampFilterExtractor(const DataFilter &filter, char matchChar = 'D');

        u32 operator()(const u32 *data, size_t size);

    private:
        DataFilter filter_;
        a2::data_filter::CacheEntry filterCache_;
};

struct EventSetup
{
    struct CrateSetup
    {
        // per crate module timestamp extractors in crate-relative module order
        std::vector<timestamp_extractor> moduleTimestampExtractors;

        // per crate module timestamp match windows in crate-relative module order
        std::vector<std::pair<s32, s32>> moduleMatchWindows;
    };

    bool enabled; // FIXME: how should this work?
    // Crate setups in crate index order => no holes in crate numbering allowed!
    std::vector<CrateSetup> crateSetups;
    // crate and crate-relative indexes of the main module which provides the reference timestamp
    std::pair<int, int> mainModule; 
};

struct ModuleAddress
{
    u8 crate;
    u8 event;
    u8 mod;
};

struct TimestampInterval
{
    s32 lower;
    s32 upper;
};

class EventBuilder
{
    public:
        explicit EventBuilder(const std::vector<EventSetup> &setup, void *userContext = nullptr);
        ~EventBuilder();

        // Push data into the eventbuilder (called after parsing and multi event splitting).
        void pushEventData(int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount);
        void pushSystemEvent(int crateIndex, const u32 *header, u32 size);

        // Attempt to build the next full events. If successful invoke the
        // callbacks to further process the assembled events. Maybe be called
        // from a different thread than the push*() methods.

        // Note: right now doesn't do any age checking or similar. This means
        // it tries to yield one assembled output event for each input event
        // from the main module.
        size_t buildEvents(Callbacks callbacks);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

enum class WindowMatchResult
{
    too_old,
    in_window,
    too_new
};

WindowMatchResult timestamp_match(u32 tsMain, u32 tsModule, const std::pair<s32, s32> &matchWindow);

} // end namespace event_builder
} // end namespace mvme

#endif /* __MVME_EVENT_BUILDER_H__ */
