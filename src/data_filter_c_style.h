#ifndef __DATA_FILTER_C_STYLE_H__
#define __DATA_FILTER_C_STYLE_H__

#include "libmvme_export.h"
#include "typedefs.h"
#include "util/bits.h"

#include <QByteArray>
#include <array>
#include <x86intrin.h>

namespace data_filter_c_style
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
    CacheEntry make_cache_entry(DataFilter filter, char marker);

    inline bool matches(DataFilter filter, u32 value, s32 wordIndex = -1)
    {
        return ((filter.matchWordIndex < 0) || (filter.matchWordIndex == wordIndex))
            && ((value & filter.matchMask) == filter.matchValue);
    }

    inline u32 extract(CacheEntry cache, u32 value)
    {
        u32 result = ((value & cache.extractMask) >> cache.extractShift);

        if (cache.needGather)
        {
            //result = bit_gather(result, cache.extractMask >> cache.extractShift);
            result = _pext_u32(result, cache.extractMask >> cache.extractShift);
        }

        return result;
    }
}

#endif /* __DATA_FILTER_C_STYLE_H__ */
