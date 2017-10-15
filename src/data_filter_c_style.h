#ifndef __DATA_FILTER_C_STYLE_H__
#define __DATA_FILTER_C_STYLE_H__

#include "libmvme_export.h"
#include "typedefs.h"
#include "util/bits.h"

#include <QByteArray>
#include <array>
#include <x86intrin.h>

namespace data_filter
{

static const s32 FilterSize = 32;

struct DataFilter
{
    std::array<char, FilterSize> filter;
    u32 matchMask  = 0;
    u32 matchValue = 0;
    s32 matchWordIndex = -1;
};

struct CacheEntry
{
    u32 extractMask = 0;
    bool needGather = false;
    u8 extractShift = 0;
    u8 extractBits  = 0;
};

DataFilter make_filter(const QByteArray &filter, s32 wordIndex = -1);

inline bool matches(DataFilter filter, u32 value, s32 wordIndex = -1)
{
    return ((filter.matchWordIndex < 0) || (filter.matchWordIndex == wordIndex))
        && ((value & filter.matchMask) == filter.matchValue);
}

CacheEntry make_cache_entry(DataFilter filter, char marker);

inline u32 extract(CacheEntry cache, u32 value)
{
    u32 result = ((value & cache.extractMask) >> cache.extractShift);

    if (cache.needGather)
    {
#ifdef __BMI2__
        result = _pext_u32(result, cache.extractMask >> cache.extractShift);
#else
        result = bit_gather(result, cache.extractMask >> cache.extractShift);
#endif
    }

    return result;
}

} // namespace data_filter

#endif /* __DATA_FILTER_C_STYLE_H__ */
