#include "combining_datafilter.h"

#include <limits>

namespace a2
{
namespace data_filter
{

u64 combine(CombiningFilter *cf, const u32 *data, u32 count)
{
    if (!validate(cf))
        return 0u;

    if (count < cf->wordCount)
        return 0u;

    const u32 mask = ((cf->flags & CombiningFilter::WordSize32)
                      ? std::numeric_limits<u32>::max()     // 0xffffffffu
                      : std::numeric_limits<u16>::max());   // 0xffffu

    u64 result = 0u;
    u16 shift = 0u;

    for (u16 wordNumber = 0; wordNumber < cf->wordCount; wordNumber++)
    {
        u16 wordIndex = ((cf->flags & CombiningFilter::ReverseCombine)
                         ? cf->wordCount - wordNumber - 1
                         : wordNumber);

        u32 dataWord  = data[wordIndex];

        result |= static_cast<u64>(dataWord & mask) << shift;
        shift += ((cf->flags & CombiningFilter::WordSize32) ? 32 : 16);
    }

    return result;
}

CombiningFilterResult extract_from_combined(CombiningFilter *cf, const u64 combined, MultiWordFilter::CacheType cacheType)
{
    assert(!is_complete(&cf->extractionFilter));
    process_data(&cf->extractionFilter, static_cast<u32>(combined));
    process_data(&cf->extractionFilter, static_cast<u32>(combined >> 32));
    bool matched = is_complete(&cf->extractionFilter);
    u64 result = matched ? extract(&cf->extractionFilter, cacheType) : 0;
    clear_completion(&cf->extractionFilter);
    return std::make_pair(result, matched);
}

CombiningFilterResult combine_and_extract(CombiningFilter *cf, const u32 *data, u32 count, MultiWordFilter::CacheType cacheType)
{
    assert(!is_complete(&cf->extractionFilter));
    u64 combined = combine(cf, data, count);
    return extract_from_combined(cf, combined, cacheType);
}

bool validate(CombiningFilter *cf)
{
    if (cf->wordCount == 0)
        return false;

    if ((cf->flags & CombiningFilter::WordSize32) && cf->wordCount > 2)
        return false;

    if (!(cf->flags & CombiningFilter::WordSize32) && cf->wordCount > 4)
        return false;

    return true;
}

CombiningFilter make_combining_filter(CombiningFilter::Flag flags,
                                      u16 wordCount,
                                      const std::vector<std::string> &filterStrings)
{
    CombiningFilter result = {};
    result.flags = flags;
    result.wordCount = wordCount;

    for (size_t i = 0; i < filterStrings.size(); i++)
    {
        add_subfilter(&result.extractionFilter, make_filter(filterStrings[i]));
    }

    return result;
}

} // namespace data_filter
} // namespace a2
