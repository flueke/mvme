#ifndef __A2_DATA_FILTER__
#define __A2_DATA_FILTER__

#include "util/bits.h"

#include <string>
#include <array>

// Huge speedup if bmi2 is enabled. Huge penalty otherwise.
#ifdef __BMI2__
#define A2_DATA_FILTER_ALWAYS_GATHER
#endif

namespace a2
{
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
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
    bool needGather = false;
    u8 extractShift = 0;
#endif
    u8 extractBits  = 0;
};

DataFilter make_filter(const std::string &filter, s32 wordIndex = -1);

inline bool matches(DataFilter filter, u32 value, s32 wordIndex = -1)
{
    return ((filter.matchWordIndex < 0) || (filter.matchWordIndex == wordIndex))
        && ((value & filter.matchMask) == filter.matchValue);
}

CacheEntry make_cache_entry(DataFilter filter, char marker);

inline u32 extract(CacheEntry cache, u32 value)
{
#ifdef A2_DATA_FILTER_ALWAYS_GATHER
    u32 result = bit_gather(value, cache.extractMask);
#else
    u32 result = ((value & cache.extractMask) >> cache.extractShift);

    if (cache.needGather)
    {
        result = bit_gather(result, cache.extractMask >> cache.extractShift);
    }
#endif
    return result;
}

std::string to_string(DataFilter filter);

} // namespace data_filter
} // namespace a2

#endif /* __A2_DATA_FILTER__ */
