#include "typedefs.h"
#include <random>
#include <pcg_random.hpp>

#include <QDebug>

/* gen hierarchical:
 * headers (need size info)
 *   module header (size info)
 *     module data
 *   ...
 *
 */

pcg32_fast rng;
std::uniform_int_distribution<u16> dist_uniform_u16(0, 0xffff);

u32 *mdpp16_scp(u32 *out, u32 outSize)
{
    u32 numWords  = 36;

    if (outSize < numWords)
        return nullptr;

    // TODO header should set proper tdc and adc bits
    *out++ = (0b0100 << 28) | numWords;

    // amplitudes
    for (s32 chan = 0; chan < 16; ++chan)
    {
        *out++ = (0b0001 << 28) | (chan << 16) | dist_uniform_u16(rng);
    }

    // times
    for (s32 chan = 16; chan < 32; ++chan)
    {
        *out++ = (0b0001 << 28) | (chan << 16) | dist_uniform_u16(rng);
    }

    u64 timestamp = 0;

    // ext ts, 16 high bits of timestamp
    // FIXME: ext ts signature not correct
    //*out++ = (0b0010 << 28) | ((timestamp >> 48) & 0xffff);

    // eoe, 30 low bits of timestamp
    *out++ = (0b11 << 30) | (timestamp & 0x3fffffff);

    return out;
}

int main(int argc, char *argv[])
{
    std::random_device rd;
    std::uniform_int_distribution<u64> dist;
    rng.seed(dist(rd));


    u64 wordsGenerated = 0;
    u64 bytesGenerated = 0;
    u32 buffer[1024];


    while (bytesGenerated < 1024 * 1024 * 1024)
    {
        u32 *endp = buffer + sizeof(buffer)/sizeof(u32);
        u32 *outp = buffer;
        u32 *prev = outp;

        while (outp)
        {
            wordsGenerated += (outp - prev);
            prev = outp;
            outp = mdpp16_scp(outp, endp - outp);
        }

        bytesGenerated = wordsGenerated * sizeof(u32);
    }

    printf("%lu\n", wordsGenerated);
    printf("%lu\n", bytesGenerated);

    return 0;
}
