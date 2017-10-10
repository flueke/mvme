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

    // Source: http://stackoverflow.com/a/757266
    inline int trailing_zeroes(uint32_t v)
    {
        int r;           // result goes here
        static const int MultiplyDeBruijnBitPosition[32] =
        {
            0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
            31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
        };
        r = MultiplyDeBruijnBitPosition[((uint32_t)((v & -v) * 0x077CB531U)) >> 27];
        return r;
    }

    // Source: http://stackoverflow.com/a/109025 (SWAR)
    inline u32 number_of_set_bits(u32 i)
    {
        // Java: use >>> instead of >>
        // C or C++: use uint32_t
        i = i - ((i >> 1) & 0x55555555);
        i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
        return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
    }
}

namespace data_filter_c_style
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
