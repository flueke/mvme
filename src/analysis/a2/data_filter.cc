#include "data_filter.h"

#include <cctype>
#include <stdexcept>

namespace
{
    inline std::string remove_spaces(const std::string &input)
    {
        std::string result;

        for (auto c: input)
        {
            if (c != ' ')
                result.push_back(c);
        }

        return result;
    }
}

namespace a2
{
namespace data_filter
{
    DataFilter make_filter(const std::string &filterRaw, s32 wordIndex)
    {
        auto filter = remove_spaces(filterRaw);

        if (filter.size() > FilterSize)
            throw std::length_error("maximum filter size of 32 exceeded");

        DataFilter result;
        result.filter.fill('X');
        result.matchWordIndex = wordIndex;

        for (s32 isrc=filter.size()-1, idst=0;
             isrc >= 0;
             --isrc, ++idst)
        {
            result.filter[idst] = filter[isrc];
        }

        for (s32 i=0; i<FilterSize; ++i)
        {
            char c = result.filter[i];

            if (c == '0' || c == '1' || c == 0 || c == 1)
                result.matchMask |= 1 << i;

            if (c == '1' || c == 1)
                result.matchValue |= 1 << i;
        }

        return result;
    }

    CacheEntry make_cache_entry(DataFilter filter, char marker)
    {
        marker = std::tolower(marker);

        CacheEntry result;

#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        bool markerSeen = false;
        bool gapSeen = false;
#endif

        for (s32 i=0; i<FilterSize; ++i)
        {
            char c = std::tolower(filter.filter[i]);

            if (c == marker)
            {
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
                if (markerSeen && gapSeen)
                {
                    // Had marker and a gap, now on marker again -> need gather step
                    result.needGather = true;
                }
                markerSeen = true;
#endif

                result.extractMask |= 1 << i;
            }
#ifndef A2_DATA_FILTER_ALWAYS_GATHER
            else if (markerSeen)
            {
                gapSeen = true;
            }
#endif
        }

#ifndef A2_DATA_FILTER_ALWAYS_GATHER
        result.extractShift = trailing_zeroes(result.extractMask);
#endif
        result.extractBits  = number_of_set_bits(result.extractMask);

        return result;
    }
} // namespace data_filter
} // namespace a2
