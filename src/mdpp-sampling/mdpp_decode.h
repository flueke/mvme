#ifndef A4204721_F6D4_407C_A19C_FB740B1013DB
#define A4204721_F6D4_407C_A19C_FB740B1013DB

#include <QUuid>
#include <chrono>
#include <mesytec-mvlc/util/int_types.h>

#include <QList>
#include <QVector>

#include "util/math.h"

#include "libmvme_mdpp_decode_export.h"

using namespace std::chrono_literals;

namespace mesytec::mvme
{
using namespace mesytec::mvlc;

static const auto MdppSamplePeriod = 12.5ns;
static constexpr u32 SampleBits = 14;
static constexpr double SampleMinValue = -1.0 * (1 << (SampleBits - 1));
static constexpr double SampleMaxValue = (1 << (SampleBits - 1)) - 1.0;

struct LIBMVME_MDPP_DECODE_EXPORT ChannelTrace
{
    // linear event number incremented on each event from the source module
    size_t eventNumber = 0;
    QUuid moduleId;
    s32 channel = -1;
    float amplitude = ::mvme::util::make_quiet_nan(); // extracted amplitude value
    float time = ::mvme::util::make_quiet_nan(); // extracted time value
    u32 header = 0; // raw module header word
    u32 amplitudeData = 0; // raw amplitude data word
    u32 timeData = 0; // raw time data word
    QVector<s16> samples; // samples are 14 bit signed, converted to and stored as 16 bit signed

    // Superhack: store (x, y) values here to enable interpolation of raw traces.
    // TODO: always use (x, y) pairs in the code related to interpolation and
    // plotting, use raw traces only when initially parsing the data
    QVector<std::pair<double, double>> interpolated; // stores interpolated samples.
};

inline bool has_raw_samples(const ChannelTrace &trace)
{
    return !trace.samples.isEmpty();
}

inline bool has_interpolated_samples(const ChannelTrace &trace)
{
    return !trace.interpolated.isEmpty();
}

inline size_t get_sample_count(const ChannelTrace &trace)
{
    return has_interpolated_samples(trace) ? trace.interpolated.size() : trace.samples.size();
}

// Clear the sample memory and reset all other fields to default values.
void LIBMVME_MDPP_DECODE_EXPORT reset_trace(ChannelTrace &trace);

// Can hold traces from multiple channels or alternatively the traces list can be
// used to store a history of traces for a particular channel.
struct LIBMVME_MDPP_DECODE_EXPORT DecodedMdppSampleEvent
{
    // Set to the linear event number when decoding data from an mdpp. leave set
    // to -1 when using this structure as a history buffer for a single channel.
    ssize_t eventNumber = -1;
    QUuid moduleId;
    u32 header = 0;
    u64 timestamp = 0;
    QList<ChannelTrace> traces;
    u8 headerModuleId = 0; // extracted from the header word for convenient access
};

DecodedMdppSampleEvent LIBMVME_MDPP_DECODE_EXPORT decode_mdpp_samples(const u32 *data, const size_t size);

}

#endif /* A4204721_F6D4_407C_A19C_FB740B1013DB */
