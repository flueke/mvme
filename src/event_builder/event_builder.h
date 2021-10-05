#ifndef __MVME_EVENT_BUILDER_H__
#define __MVME_EVENT_BUILDER_H__

#include <functional>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "analysis/a2/a2_data_filter.h"

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

struct EventBuilder
{
    public:

        // Push data into the eventbuilder (called after parsing and multi event splitting)
        void pushEventData(int crateIndex, /*int eventIndex,*/ const ModuleData *moduleDataList, unsigned moduleCount);
        void pushSystemEvent(int crateIndex, const u32 *header, u32 size);

        // Attempt to build the next full event. If successful invoke the callbacks
        // to further process the assembled event.
        void buildEvent(Callbacks &callbacks);

    private:
        ModuleAddress mainModule_;
        std::vector<std::vector<u32>> eventBuffer_;

};

} // end namespace event_builder

#endif /* __MVME_EVENT_BUILDER_H__ */
