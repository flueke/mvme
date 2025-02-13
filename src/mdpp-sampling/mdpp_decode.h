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


union LIBMVME_MDPP_DECODE_EXPORT TraceHeader
{
    struct Parts
    {
        // Padding bits.
        u32 pad_: 4;

        // Internal firmware debug flag. Unused.
        u32 debug: 1;

        // The contents of 0x614A:
        // Sampling configuration (register 0x614A):
        // bits 0-2: sample source (depends on firmware type, see the init scripts and the data sheet).
        // bit 7 set: no offset correction
        // bit 6 set: no resampling
        // Other bits are not used yet.
        u32 config: 8;

        // Phase correction factor. [0, 512] normalized to [0, 1) in the analysis
        u32 phase: 9;

        // Length of the trace in words. Sample count is 2 x length. Currently
        // not used by the decoder.
        u32 length: 10;
    };

    enum PartIndex
    {
        Debug = 0,
        Config = 1,
        Phase = 2,
        Length = 3
    };

    static const LIBMVME_MDPP_DECODE_EXPORT std::array<const char *, 4> PartNames;
    static const LIBMVME_MDPP_DECODE_EXPORT std::array<unsigned, 4> PartBits;

    Parts parts;
    u32 value = 0;
};

// Register 0x614A / the 'config' field in the trace header.
struct LIBMVME_MDPP_DECODE_EXPORT SamplingSettings
{
    static const u32 SourceMask             = 0b11;
    static const u32 NoResampling           = 1u << 6; // if set do phase correction in software
    static const u32 NoOffsetCorrection     = 1u << 7;
};

struct LIBMVME_MDPP_DECODE_EXPORT ChannelTrace
{
    // mvme moduleId
    QUuid moduleId;
    // linear event number incremented on each event from the source module
    size_t eventNumber = 0;
    // source channel number
    s32 channel = -1;
    // raw module header word
    u32 moduleHeader = 0;
    QVector<s16> samples; // samples are 14 bit signed, converted to and stored as 16 bit signed
    TraceHeader traceHeader;
};

inline bool has_raw_samples(const ChannelTrace &trace)
{
    return !trace.samples.isEmpty();
}

inline size_t get_raw_sample_count(const ChannelTrace &trace)
{
    return trace.samples.size();
}

// Clear the sample memory and reset all other fields to default values.
void LIBMVME_MDPP_DECODE_EXPORT reset_trace(ChannelTrace &trace);

// Can hold traces from multiple channels or alternatively the `traces` list can
// be used to store a history of traces for a particular channel.
struct LIBMVME_MDPP_DECODE_EXPORT DecodedMdppSampleEvent
{
    u32 header = 0;             // raw mdpp module header
    u64 timestamp = 0;          // extracted timestamp. both standard and extended timestamp are taken into account
    QList<ChannelTrace> traces; // decoded trace data
    u8 headerModuleId = 0;      // extracted from the header word
    QUuid moduleId;             // optional mvme module id
    std::string moduleType;     // optional module type string used when decoding the event
    std::vector<u32> inputData; // optional copy of the raw input data used to decode this event
    ssize_t eventNumber = -1;   // optional linear event number of the decoded event. Leave set
                                // to -1 when using this structure as a history buffer for a
                                // single channel and use ChannelTrace.eventNumber instead to
                                // keep track of the origin event number of a trace.
};

using logger_function = std::function<void (const std::string &level, const std::string &message)>;

// Pass the module type as a string. Currently supported types: mdpp16_scp,
// mdpp16_qdc, mdpp32_scp and mdpp32_qdc.
DecodedMdppSampleEvent LIBMVME_MDPP_DECODE_EXPORT decode_mdpp_samples(
    const u32 *data, const size_t size, const char *moduleType, logger_function logger_fun = {});

using DecoderFunction = std::function<DecodedMdppSampleEvent (
    const u32 *data, const size_t size, mdpp_sampling::logger_function logger_fun)>;

// Get the decoder function for the specified module type. Can use this to get
// the decoder once, instead of re-checking the module type for each event.
DecoderFunction LIBMVME_MDPP_DECODE_EXPORT get_decoder_function(const char *moduleType);

using TraceBuffer = QList<ChannelTrace>;
using ModuleTraceHistory = std::vector<TraceBuffer>; // indexed by the traces channel number
using TraceHistoryMap = QMap<QUuid, ModuleTraceHistory>;

std::string LIBMVME_MDPP_DECODE_EXPORT sampling_config_to_string(u32 config);

}

#endif /* A4204721_F6D4_407C_A19C_FB740B1013DB */
