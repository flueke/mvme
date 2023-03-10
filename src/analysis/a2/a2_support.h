#ifndef SRC_ANALYSIS_A2_A2_SUPPORT_H
#define SRC_ANALYSIS_A2_A2_SUPPORT_H

#include "util/nan.h"
#include "util/typedefs.h"

namespace a2
{

inline s64 convert_to_signed(u64 value, unsigned numDataBits)
{
    const u64 signMask = 1lu << (numDataBits - 1); // isolate the sign bit

    s64 result = 0;

    if (value & signMask)
    {
        const u64 dataMask = signMask - 1; // has all bits lower than the sign bit set
        const u64 highOnes = std::numeric_limits<u64>::max() & ~dataMask; // all ones but the lowest numDataBits masked out
        u64 uresult = highOnes | value; // set the low bits to the original input value
        result = *reinterpret_cast<s64 *>(&uresult); // interpret as twos-complement signed
    }
    else
        result = static_cast<s64>(value);

    return result;
}

}

#endif // SRC_ANALYSIS_A2_A2_SUPPORT_H
