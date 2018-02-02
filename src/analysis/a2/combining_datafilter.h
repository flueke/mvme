#ifndef __A2_COMBINING_DATAFILTER_H__
#define __A2_COMBINING_DATAFILTER_H__

/*
 * Combining filters (draft name)
 * -----------------------------
 *
 * Input:
 * - Number of words to combine
 * - 16 or 32 bit word length
 * - Word combining: from first word or from last word (natural order or reverse),
 *   destination word is always filled from low bits to high bits
 * - After combining step use a data filter to optionally match certain bits and
 *   for address and value extraction.
 *
 *   Note: as for DataFilter and MultiWordFilter address bits are not required.
 *   the filter will then always yield a single output value with address=0.
 *
 *   Also: address extraction would only really make sense if the filter is
 *   repeated N times. Each repetition would start where the previous filter
 *   left off.
 *
 *   Repeated application of the filter should be handled on the outside. The
 *   number of data words consumed by the filter is known (wordCount) so the
 *   analysis implementation can apply the filter again using the wordCount as
 *   an offset.
 *
 * - Last step: convert extracted value to double. Should a random value be
 *   added? Yes, as it only affects the part after the decimal period which is
 *   not transmitted with the original data.
 *
 * Limits:
 * - input words are 32-bit
 * - intermediate word is 64-bit to allow combining up to two 32-bit or four 16-bit words
 *
 *
 * TODO:
 * - implement single word data filters for u64
 *   Reason: the combined filter uses the member multiwordfilter just to
 *   process the combined u64 by splitting it up into two u32s.
 * - Error reporting
 */

#include "multiword_datafilter.h"

#include <string>
#include <vector>

namespace a2
{
namespace data_filter
{

struct CombiningFilter
{
    using Flag = u8;

    static const Flag NoFlag            = 0u;

    /* Specifies the size of the parts that form the combined intermediate data
     * word. If set the full 32 bits of the input data word are used, otherwise
     * the low 16 bits are used. */
    static const Flag WordSize32        = 1u << 0;

    /* If not set the combined word will be filled from first input word to
     * last, otherwise the order is reversed so the last input word fills the
     * low bits and the first input word fills the high bits. */
    static const Flag ReverseCombine    = 1u << 1;

    /* The filter used for final data extraction. A multiword filter is used as
     * it allows to pass two 32 bit values and returns a 64 bit value. */
    MultiWordFilter extractionFilter; // TODO: replace this by a single 64 bit DataFilter once those are implemented

    Flag flags;

    /* The number of input words to combine. Max 2 if WordSize32 is set,
     * otherwise 4. */
    u8 wordCount;
};

CombiningFilter make_combining_filter(CombiningFilter::Flag flags,
                                      u16 wordCount,
                                      const std::vector<std::string> &filterStrings = {});

bool validate(CombiningFilter *cf);

/* Result of a combining filter operation. The first item is the extracted
 * value, the second item is true if the MultiWordFilter completed, false
 * otherwise. */
using CombiningFilterResult = std::pair<u64, bool>;

/* Combines input data words according to the filter specification and returns
 * the combined result. */
u64 combine(CombiningFilter *cf, const u32 *data, u32 count);

/* Takes a combined data word and extracts the data specified by the given
 * CacheType. Pass MultiWordFilter:CacheA for the address value,
 * MultiWordFilter::CacheD for the data value. */
CombiningFilterResult extract_from_combined(CombiningFilter *cf, const u64 combinedData,
                                            MultiWordFilter::CacheType cacheType);

/* Performs both the combine and extraction steps. */
CombiningFilterResult combine_and_extract(CombiningFilter *cf, const u32 *data, u32 count,
                                          MultiWordFilter::CacheType cacheType);

/* Shortcut to combine and extract the data value. */
inline CombiningFilterResult combine_and_extract_value(CombiningFilter *cf, const u32 *data, u32 count)
{
    return combine_and_extract(cf, data, count, MultiWordFilter::CacheD);
}

/* Shortcut to combine and extract the address value. */
inline CombiningFilterResult combine_and_extract_address(CombiningFilter *cf, const u32 *data, u32 count)
{
    return combine_and_extract(cf, data, count, MultiWordFilter::CacheA);
}

inline size_t get_extract_bits(CombiningFilter *cf, MultiWordFilter::CacheType cacheType)
{
    return get_extract_bits(&cf->extractionFilter, cacheType);
}

} // namespace data_filter
} // namespace a2

#endif /* __A2_COMBINING_DATAFILTER_H__ */
