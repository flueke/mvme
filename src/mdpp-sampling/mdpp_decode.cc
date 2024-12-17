#include "mdpp_decode.h"

#include <optional>

#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/logging.h>

#include "util/math.h"
#include "analysis/a2/a2_support.h"

#define SAM_ASSERT_WARN(cond) if (!(cond)) spdlog::warn("Assertion failed: {}", #cond)

namespace mesytec::mvme::mdpp_sampling
{

const std::array<const char *, 4> TraceHeader::PartNames = { "debug", "config", "phase", "length" };
const std::array<unsigned, 4> TraceHeader::PartBits = { 1, 8, 9, 10 };

namespace
{

using FilterWithCaches = mvlc::util::FilterWithCaches;
using mvlc::util::make_filter_with_caches;

// CommonFilters contains filters used for all supported module types.
//
// The sub-structs contain "channel address yielding" filters only. These are
// tested in the order they appear in the 'filters' array. The first matching
// filter contributes the current channel address during decoding.

struct CommonFilters
{
	const FilterWithCaches fModuleId        = make_filter_with_caches("0100 XXXX DDDD DDDD XXXX XXXX XXXX XXXX");
	const FilterWithCaches fTriggerTime     = make_filter_with_caches("0001 XXXX X100 000A DDDD DDDD DDDD DDDD");
	const FilterWithCaches fTimeStamp       = make_filter_with_caches("11DD DDDD DDDD DDDD DDDD DDDD DDDD DDDD");
	const FilterWithCaches fExtentedTs      = make_filter_with_caches("0010 XXXX XXXX XXXX DDDD DDDD DDDD DDDD");

    // Samples header: D = debug[1:0], C = config[7:0], P = phase[8:0], L = length[9:0]
    // FIXME: still one bit too many! Took one away from the debug field.
    const FilterWithCaches fSamplesHeader   = make_filter_with_caches("0011 DCCC CCCC CPPP PPPP PPLL LLLL LLLL");
	const FilterWithCaches fSamples         = make_filter_with_caches("0011 DDDD DDDD DDDD DDDD DDDD DDDD DDDD");
};

struct Mdpp16ScpFilters: public CommonFilters
{
	const FilterWithCaches fChannelTime    = make_filter_with_caches("0001 XXXX XX01 AAAA DDDD DDDD DDDD DDDD");
	const FilterWithCaches fAmplitude      = make_filter_with_caches("0001 XXXX PO00 AAAA DDDD DDDD DDDD DDDD");

    const std::array<FilterWithCaches, 2> channelFilters =
    {
        fChannelTime,
        fAmplitude
    };

    const char *moduleType = "mdpp16_scp";
};

struct Mdpp16QdcFilters: public CommonFilters
{
    const FilterWithCaches fChannelTime      = make_filter_with_caches("0001 XXXX XX01 AAAA DDDD DDDD DDDD DDDD");
    const FilterWithCaches fIntegrationLong  = make_filter_with_caches("0001 XXXX XX00 AAAA DDDD DDDD DDDD DDDD");
    const FilterWithCaches fIntegrationShort = make_filter_with_caches("0001 XXXX XX11 AAAA DDDD DDDD DDDD DDDD");

    const std::array<FilterWithCaches, 3> channelFilters =
    {
        fChannelTime,
        fIntegrationLong,
        fIntegrationShort
    };

    const char *moduleType = "mdpp16_qdc";
};

struct Mdpp32ScpFilters: public CommonFilters
{
	const FilterWithCaches fChannelTime    = make_filter_with_caches("0001 XXXP O00A AAAA DDDD DDDD DDDD DDDD");
	const FilterWithCaches fAmplitude      = make_filter_with_caches("0001 XXXP O01A AAAA DDDD DDDD DDDD DDDD");

    const std::array<FilterWithCaches, 2> channelFilters =
    {
        fChannelTime,
        fAmplitude
    };

    const char *moduleType = "mdpp32_scp";
};

struct Mdpp32QdcFilters: public CommonFilters
{
    const FilterWithCaches fChannelTime      = make_filter_with_caches("0001 XXXX X01A AAAA DDDD DDDD DDDD DDDD");
    const FilterWithCaches fIntegrationLong  = make_filter_with_caches("0001 XXXX X00A AAAA DDDD DDDD DDDD DDDD");
    const FilterWithCaches fIntegrationShort = make_filter_with_caches("0001 XXXX X11A AAAA DDDD DDDD DDDD DDDD");

    const std::array<FilterWithCaches, 3> channelFilters =
    {
        fChannelTime,
        fIntegrationLong,
        fIntegrationShort
    };

    const char *moduleType = "mdpp32_qdc";
};

static const Mdpp16ScpFilters mdpp16ScpFilters;
static const Mdpp16QdcFilters mdpp16QdcFilters;
static const Mdpp32ScpFilters mdpp32ScpFilters;
static const Mdpp32QdcFilters mdpp32QdcFilters;

// Used if no logger function is provided. Set to error level to avoid spamming
// the console.
void default_logger_fun(const std::string &level, const std::string &message)
{
    auto logger = mvlc::get_logger("mdpp_sam_decode");
    logger->set_level(spdlog::level::err);
    logger->log(spdlog::level::from_str(level), message);
}

}

void reset_trace(ChannelTrace &trace)
{
    trace.eventNumber = 0;
    trace.moduleId = QUuid();
    trace.moduleHeader = 0;
    trace.samples.clear();
    trace.traceHeader = {};
}

// Extract a timestamp value from mesytec vme module data. Both standard and
// extended timestamp words are considered. Null fillwords are skipped. Only the
// first hits for the timestamp and extended timestamp filter masks are
// considered. Searching is done in reverse order as the timestamps should be at
// or near the end of the data.
template<typename Filters>
std::optional<u64> extract_timestamp(const u32 *data, const size_t size, const Filters &filters, logger_function logger_fun)
{
    auto pred_ts = [&filters](u32 word) { return mvlc::util::matches(filters.fTimeStamp, word); };
    auto pred_ext_ts = [&filters](u32 word) { return mvlc::util::matches(filters.fExtentedTs, word); };

    std::basic_string_view<u32> dataView(data, size);
    std::optional<u64> ret;

    if (auto it = std::find_if(std::rbegin(dataView), std::rend(dataView), pred_ts);
        it != std::rend(dataView))
    {
        ret = mvlc::util::extract(filters.fTimeStamp, *it, 'D');
        logger_fun("trace", fmt::format("timestamp matched (30 low bits): 0b{:030b}, 0x{:08x}", *ret, *ret));
    }
    else
    {
        return ret;
    }

    if (auto it = std::find_if(std::rbegin(dataView), std::rend(dataView), pred_ext_ts);
        it != std::rend(dataView) && ret.has_value())
    {
        // optional 16 high bits of the extended timestamp if enabled
        auto value = *mvlc::util::extract(filters.fExtentedTs, *it, 'D');
        ret = *ret | (static_cast<std::uint64_t>(value) << 30);
        logger_fun("trace", fmt::format("extended timestamp matched (16 high bits): 0b{:016b}, 0x{:08x}", value, value));
    }
    else
    {
        logger_fun("trace", fmt::format("decode_mdpp_samples: no extended timestamp present"));
    }

    return ret;
}

template<typename Filters>
DecodedMdppSampleEvent decode_mdpp_samples_impl(const u32 *data, const size_t size, const Filters &filters, logger_function logger_fun)
{
    if (!logger_fun)
        logger_fun = default_logger_fun;

    std::basic_string_view<u32> dataView(data, size);

    logger_fun("trace", fmt::format("decode_mdpp_samples: input.size={}, input={:#010x}", dataView.size(), fmt::join(dataView, " ")));

    DecodedMdppSampleEvent ret{};

    // Need at least the module header and the EndOfEvent word.
    if (size < 2)
    {
        logger_fun("warn", "decode_mdpp_samples: input data size < 2, returning null result");
        return {};
    }

    // The module header word is always the first word. Store it and extract the module ID.
    if (mvlc::util::matches(filters.fModuleId, data[0]))
    {
        ret.header = data[0];
        ret.headerModuleId = *mvlc::util::extract(filters.fModuleId, data[0], 'D');
    }
    else
    {
        logger_fun("warn", "decode_mdpp_samples: no module header present");
    }

    auto timestamp = extract_timestamp(data, size, filters, logger_fun);

    if (timestamp.has_value())
    {
        ret.timestamp = *timestamp;
    }
    else
    {
        logger_fun("warn", "decode_mdpp_samples: no timestamp present");
    }

    ChannelTrace currentTrace;
    size_t totalSamples = 0;

	for (auto wordPtr = data+1, dataEnd = data + size; wordPtr < dataEnd; ++wordPtr)
    {
        // The data words containing amplitude, channel time, integration_long
        // or integration_short are guaranteed to come before the samples for
        // the respective channel.
        // This means we will always have a valid channel number when starting
        // to process sample data.

        // Try the filters in order. Stop at the first match.
        const FilterWithCaches *channelFilter = nullptr;
        for (const auto &filter: filters.channelFilters)
        {
            if (mvlc::util::matches(filter, *wordPtr))
            {
                channelFilter = &filter;
                break;
            }
        }

        if (channelFilter)
        {
            auto &theFilter = *channelFilter;

            // Note: extract yields unsigned, but the address values do easily
            // fit in signed 32-bit integers.
			s32 addr = *mvlc::util::extract(theFilter, *wordPtr, 'A');
			auto value = *mvlc::util::extract(theFilter, *wordPtr, 'D');

            if (currentTrace.channel >= 0 && currentTrace.channel != addr)
            {
                // The current channel number changed which means we're done
                // with this trace and can prepare for the next one.
                logger_fun("trace", fmt::format("decode_mdpp_samples: Finished decoding a channel trace: channel={}, #samples={}, samples={}",
                    currentTrace.channel, currentTrace.samples.size(), fmt::join(currentTrace.samples, ", ")));

                totalSamples += currentTrace.samples.size();
                ret.traces.push_back(currentTrace); // store the old trace

                // begin the new trace
                reset_trace(currentTrace);
            }

            currentTrace.channel = addr;
            currentTrace.moduleHeader = ret.header;
        }
		else if (mvlc::util::matches(filters.fSamples, *wordPtr))
		{
            if (currentTrace.channel < 0)
            {
                logger_fun("error", "decode_mdpp_samples: sample datum without prior amplitude or channel time data");
                return {};
            }

            if (currentTrace.traceHeader.value == 0)
            {
                // No header seen yet for this trace so this match is the samples header.

                TraceHeader traceHeader;

                traceHeader.parts.debug = *mvlc::util::extract(filters.fSamplesHeader, *wordPtr, 'D');
                traceHeader.parts.config = *mvlc::util::extract(filters.fSamplesHeader, *wordPtr, 'C');
                traceHeader.parts.phase = *mvlc::util::extract(filters.fSamplesHeader, *wordPtr, 'P');
                traceHeader.parts.length = *mvlc::util::extract(filters.fSamplesHeader, *wordPtr, 'L');

                // Sadly cannot (yet) use bitfield members directly with spdlog.
                auto thDebug = traceHeader.parts.debug;
                auto thConfig = traceHeader.parts.config;
                auto thPhase = traceHeader.parts.phase;
                auto thLength = traceHeader.parts.length;

                logger_fun("trace", fmt::format("decode_mdpp_samples: Found trace header: debug={}, config={}, phase={}, length={}",
                    thDebug, thConfig, thPhase, thLength));

                currentTrace.traceHeader = traceHeader;
            }
            else
            {
                // The 14 high bits are the even sample, the 14 low bits the odd one.
                constexpr u32 SampleMask = (1u << SampleBits) - 1;

                // Extract the raw sample values
                auto value = *mvlc::util::extract(filters.fSamples, *wordPtr, 'D');
                u32 evenRaw = value & SampleMask;
                u32 oddRaw  = (value >> SampleBits) & SampleMask;

                // Now interpret the 14 bit values as signed.
                s64 evenSigned = a2::convert_to_signed(evenRaw, SampleBits);
                s64 oddSigned = a2::convert_to_signed(oddRaw, SampleBits);

                currentTrace.samples.push_back(evenSigned);
                currentTrace.samples.push_back(oddSigned);
            }
        }
        else if (*wordPtr == 0u // fillword
                || mvlc::util::matches(filters.fTimeStamp, *wordPtr)
                || mvlc::util::matches(filters.fExtentedTs, *wordPtr)
                || mvlc::util::matches(filters.fTriggerTime, *wordPtr))
        {
            // Explicit test for fillword, timestamp and other known data words
            // to avoid issuing unneeded warnings.
        }
        else
        {
            // Hit an unexpected data word.
            logger_fun("warn", fmt::format("decode_mdpp_samples: No filter match for word #{}: hex={:#010x}, bin={:#b}",
                std::distance(data, wordPtr), *wordPtr, *wordPtr));
        }
    }

    // Handle a possible last trace that was decoded but not yet moved into the
    // result.
    if (currentTrace.channel >= 0)
    {
        logger_fun("trace", fmt::format("decode_mdpp_samples: Finished decoding a channel trace: channel={}, #samples={}, samples={}",
            currentTrace.channel, currentTrace.samples.size(), fmt::join(currentTrace.samples, ", ")));
        ret.traces.push_back(currentTrace);
    }

    logger_fun("trace", fmt::format("decode_mdpp_samples finished decoding: header={:#010x}, timestamp={}, moduleId={:#04x}, #traces={}, #totalSamples={}",
                 ret.header, ret.timestamp, ret.headerModuleId, ret.traces.size(), totalSamples));

    // Store a copy of the raw input data in the result. Used in the UI to rerun
    // the decoder for debugging purposes.
    std::copy(data, data + size, std::back_inserter(ret.inputData));
    ret.moduleType = filters.moduleType;

    return ret;
}

DecoderFunction get_decoder_function(const char *moduleType)
{
    if (strcmp(moduleType, "mdpp16_scp") == 0)
    {
        return [] (const u32 *data, const size_t size, logger_function logger_fun)
        {
            return decode_mdpp_samples_impl(data, size, mdpp16ScpFilters, logger_fun);
        };
    }
    else if (strcmp(moduleType, "mdpp16_qdc") == 0)
    {
        return [] (const u32 *data, const size_t size, logger_function logger_fun)
        {
            return decode_mdpp_samples_impl(data, size, mdpp16QdcFilters, logger_fun);
        };
    }
    else if (strcmp(moduleType, "mdpp32_scp") == 0)
    {
        return [] (const u32 *data, const size_t size, logger_function logger_fun)
        {
            return decode_mdpp_samples_impl(data, size, mdpp32ScpFilters, logger_fun);
        };
    }
    else if (strcmp(moduleType, "mdpp32_qdc") == 0)
    {
        return [] (const u32 *data, const size_t size, logger_function logger_fun)
        {
            return decode_mdpp_samples_impl(data, size, mdpp32QdcFilters, logger_fun);
        };
    }

    return {};
}

DecodedMdppSampleEvent decode_mdpp_samples(const u32 *data, const size_t size, const char *moduleType, logger_function logger_fun_)
{
    auto logger_fun = logger_fun_ ? logger_fun_ : default_logger_fun;

    DecodedMdppSampleEvent result;

    if (auto decoder = get_decoder_function(moduleType))
    {
        result = decoder(data, size, logger_fun);
    }
    else
    {
        logger_fun("error", fmt::format("decode_mdpp_samples: unknown module type '{}'", moduleType));
        return {};
    }

    return result;
}

}
