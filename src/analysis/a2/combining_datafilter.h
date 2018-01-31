#ifndef __A2_COMBINING_DATAFILTER_H__
#define __A2_COMBINING_DATAFILTER_H__

/*
 * Combining filters (draft name)
 * -----------------------------
 *
 * Input:
 * - number of words to combine
 * - 16 or 32 bit word length
 * - word combining: from first word or from last word (natural order or reverse),
 *   destination word is always filled from low bits to high bits
 * - after combining step use a data filter to optionally match certain bits and
 *   for address and value extraction
 *   address bits are not required. the filter will then always yield a single
 *   output value
 *   Also: address extraction would only really make sense if the filter is
 *   repeated N times. Each repetition would start where the previous filter left
 *   off
 * - last step: convert extracted value to double
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
    using Flag = u16;

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
    MultiWordFilter extractionFilter;

    Flag flags;

    /* The number of input words to combine. Max 2 if WordSize32 is set,
     * otherwise 4. */
    u16 wordCount;
};

CombiningFilter make_combining_filter(CombiningFilter::Flag flags,
                                      u16 wordCount,
                                      const std::vector<std::string> &filterStrings = {});

bool validate(CombiningFilter *cf);

u64 combine(CombiningFilter *cf, const u32 *data, u32 count);
u64 combine_and_extract(CombiningFilter *cf, const u32 *data, u32 count, MultiWordFilter::CacheType cacheType);

inline u64 combine_and_extract_value(CombiningFilter *cf, const u32 *data, u32 count)
{
    return combine_and_extract(cf, data, count, MultiWordFilter::CacheD);
}

inline u64 combine_and_extract_address(CombiningFilter *cf, const u32 *data, u32 count)
{
    return combine_and_extract(cf, data, count, MultiWordFilter::CacheA);
}

} // namespace data_filter
} // namespace a2

#endif /* __A2_COMBINING_DATAFILTER_H__ */
