#include "data_filter_c_style.h"

#include <cctype>
#include <stdexcept>

namespace
{
    inline QByteArray remove_spaces(const QByteArray input)
    {
        QByteArray result;

        for (auto c: input)
        {
            if (c != ' ')
                result.push_back(c);
        }

        return result;
    }
}

namespace data_filter
{
    DataFilter make_filter(const QByteArray &filterRaw, s32 wordIndex)
    {
        QByteArray filter = remove_spaces(filterRaw);

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

        bool markerSeen = false;
        bool gapSeen = false;

        for (s32 i=0; i<FilterSize; ++i)
        {
            char c = std::tolower(filter.filter[i]);

            if (c == marker)
            {
                if (markerSeen && gapSeen)
                {
                    // Had marker and a gap, now on marker again -> need gather step
                    result.needGather = true;
                }

                result.extractMask |= 1 << i;
                markerSeen = true;
            }
            else if (markerSeen)
            {
                gapSeen = true;
            }
        }

        result.extractShift = trailing_zeroes(result.extractMask);
        result.extractBits  = number_of_set_bits(result.extractMask);

        return result;
    }
}
