#ifndef A4204721_F6D4_407C_A19C_FB740B1013DB
#define A4204721_F6D4_407C_A19C_FB740B1013DB

#include <QUuid>
#include <chrono>
#include <mesytec-mvlc/util/int_types.h>

#include <QList>
#include <QVector>

#include "typedefs.h"
#include "util/math.h"

#include "libmvme_mdpp_decode_export.h"

using namespace std::chrono_literals;

namespace mesytec::mvme::mdpp_sampling
{

static const auto MdppDefaultSamplePeriod = 12.5;
static constexpr u32 SampleBits = 14;
static constexpr double SampleMinValue = -1.0 * (1 << (SampleBits - 1));
static constexpr double SampleMaxValue = (1 << (SampleBits - 1)) - 1.0;

struct LIBMVME_MDPP_DECODE_EXPORT ChannelTrace
{
    union TraceHeader
    {
        struct
        {
            u32 pad: 4;
            u32 debug: 1;
            u32 config: 8;
            u32 phase: 9;
            u32 length: 10;
        };
        u32 value = 0;
    };
    // linear event number incremented on each event from the source module
    size_t eventNumber = 0;
    QUuid moduleId;
    s32 channel = -1;
    float amplitude = util::make_quiet_nan(); // extracted amplitude value
    float time = util::make_quiet_nan(); // extracted time value
    u32 moduleHeader = 0; // raw module header word
    u32 amplitudeData = 0; // raw amplitude data word
    u32 timeData = 0; // raw time data word
    double dtSample = MdppDefaultSamplePeriod;

    // Store both, the unmodified raw samples and the interpolated curve data.

    QVector<s16> samples; // samples are 14 bit signed, converted to and stored as 16 bit signed

    // Store (x, y) values here to allow interpolation of raw traces.
    QVector<std::pair<double, double>> interpolated;
    TraceHeader traceHeader;
};

inline bool has_raw_samples(const ChannelTrace &trace)
{
    return !trace.samples.isEmpty();
}

inline bool has_interpolated_samples(const ChannelTrace &trace)
{
    return !trace.interpolated.isEmpty();
}

inline size_t get_raw_sample_count(const ChannelTrace &trace)
{
    return trace.samples.size();
}

inline size_t get_interpolated_sample_count(const ChannelTrace &trace)
{
    return trace.interpolated.size();
}

// Clear the sample memory and reset all other fields to default values.
void LIBMVME_MDPP_DECODE_EXPORT reset_trace(ChannelTrace &trace);

// Can hold traces from multiple channels or alternatively the traces list can be
// used to store a history of traces for a particular channel.
struct LIBMVME_MDPP_DECODE_EXPORT DecodedMdppSampleEvent
{
    // Set to the linear event number when decoding data from an mdpp. Leave set
    // to -1 when using this structure as a history buffer for a single channel.
    ssize_t eventNumber = -1;
    QUuid moduleId;
    u32 header = 0;
    u64 timestamp = 0;
    QList<ChannelTrace> traces;
    u8 headerModuleId = 0; // extracted from the header word for convenient access
};

DecodedMdppSampleEvent LIBMVME_MDPP_DECODE_EXPORT decode_mdpp16_scp_samples(const u32 *data, const size_t size);
DecodedMdppSampleEvent LIBMVME_MDPP_DECODE_EXPORT decode_mdpp32_scp_samples(const u32 *data, const size_t size);
// Pass the module type as a string. Currently supported types: mdpp16_scp and mdpp32_scp
DecodedMdppSampleEvent LIBMVME_MDPP_DECODE_EXPORT decode_mdpp_samples(const u32 *data, const size_t size, const char *moduleType);

using TraceBuffer = QList<ChannelTrace>;
using ModuleTraceHistory = std::vector<TraceBuffer>; // indexed by the traces channel number
using TraceHistoryMap = QMap<QUuid, ModuleTraceHistory>;

}

#endif /* A4204721_F6D4_407C_A19C_FB740B1013DB */
