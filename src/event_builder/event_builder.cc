#include "event_builder/event_builder.h"

#include <boost/circular_buffer.hpp>

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
    else if (index_ < size && matches(filter_, data[index_]))
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

void EventBuilder::pushEventData(void *userContext, int crateIndex, int eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
}

void EventBuilder::pushSystemEvent(void *userContext, int crateIndex, const u32 *header, u32 size)
{
}

void EventBuilder::buildEvents(Callbacks &callbacks)
{
}


} // end namespace event_builder
} // end namespace mvme
