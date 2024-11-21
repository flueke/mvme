#include "mdpp_decode.h"

#include <optional>

#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/logging.h>

#include "util/math.h"
#include "analysis/a2/a2_support.h"

#if 0
#define SAM_ASSERT(cond) assert(cond)
#else
#define SAM_ASSERT(cond)
#endif

namespace mesytec::mvme::mdpp_sampling
{

namespace
{

using FilterWithCaches = mvlc::util::FilterWithCaches;
using mvlc::util::make_filter_with_caches;

struct CommonFilters
{
	const FilterWithCaches fModuleId        = make_filter_with_caches("0100 XXXX DDDD DDDD XXXX XXXX XXXX XXXX");
	const FilterWithCaches fTriggerTime     = make_filter_with_caches("0001 XXXX X100 000A DDDD DDDD DDDD DDDD");
	const FilterWithCaches fTimeStamp       = make_filter_with_caches("11DD DDDD DDDD DDDD DDDD DDDD DDDD DDDD");
	const FilterWithCaches fExtentedTs      = make_filter_with_caches("0010 XXXX XXXX XXXX DDDD DDDD DDDD DDDD");
    // Samples header: D = debug[1:0], C = config[7:0], P = phase[8:0], L = length[9:0]
    const FilterWithCaches fSamplesHeader   = make_filter_with_caches("0011 DCCC CCCC CPPP PPPP PPLL LLLL LLLL");
	const FilterWithCaches fSamples         = make_filter_with_caches("0011 DDDD DDDD DDDD DDDD DDDD DDDD DDDD");
};

struct Mdpp16ScpFilters: public CommonFilters
{
	const FilterWithCaches fChannelTime    = make_filter_with_caches("0001 XXXX XX01 AAAA DDDD DDDD DDDD DDDD");
	const FilterWithCaches fAmplitude      = make_filter_with_caches("0001 XXXX PO00 AAAA DDDD DDDD DDDD DDDD");
};

struct Mdpp32ScpFilters: public CommonFilters
{
	const FilterWithCaches fChannelTime    = make_filter_with_caches("0001 XXXP O00A AAAA DDDD DDDD DDDD DDDD");
	const FilterWithCaches fAmplitude      = make_filter_with_caches("0001 XXXP O01A AAAA DDDD DDDD DDDD DDDD");
};

static const Mdpp16ScpFilters mdpp16ScpFilters;
static const Mdpp32ScpFilters mdpp32ScpFilters;

}

void reset_trace(ChannelTrace &trace)
{
    trace.eventNumber = 0;
    trace.moduleId = QUuid();
    trace.amplitude = mvme::util::make_quiet_nan();
    trace.time = mvme::util::make_quiet_nan();
    trace.header = 0;
    trace.amplitudeData = 0;
    trace.timeData = 0;
    trace.samples.clear();
}

template<typename Filters>
std::optional<u64> extract_timestamp(const u32 *data, const size_t size, const Filters &filters)
{

    auto pred_ts = [&filters](u32 word) { return mvlc::util::matches(filters.fTimeStamp, word); };
    auto pred_ext_ts = [&filters](u32 word) { return mvlc::util::matches(filters.fExtentedTs, word); };

    std::basic_string_view<u32> dataView(data, size);
    std::optional<u64> ret;

    if (auto it = std::find_if(std::rbegin(dataView), std::rend(dataView), pred_ts);
        it != std::rend(dataView))
    {
        ret = mvlc::util::extract(filters.fTimeStamp, *it, 'D');
        if (ret.has_value())
            spdlog::trace("timestamp matched (30 low bits): 0b{:030b}, 0x{:08x}", *ret, *ret);
    }
    else
    {
        SAM_ASSERT(!"decode_mdpp_samples: fTimeStamp");
        return ret;
    }

    if (auto it = std::find_if(std::rbegin(dataView), std::rend(dataView), pred_ext_ts);
        it != std::rend(dataView) && ret.has_value())
    {
        // optional 16 high bits of the extended timestamp if enabled
        auto value = *mvlc::util::extract(filters.fExtentedTs, *it, 'D');
        spdlog::trace("extended timestamp matched (16 high bits): 0b{:016b}, 0x{:08x}", value, value);
        ret = *ret | (static_cast<std::uint64_t>(value) << 30);
    }
    else
    {
        spdlog::trace("decode_mdpp_samples: no extended timestamp present");
    }

    return ret;
}

template<typename Filters>
DecodedMdppSampleEvent decode_mdpp_samples_impl(const u32 *data, const size_t size, const Filters &filters)
{
    std::basic_string_view<u32> dataView(data, size);

    spdlog::trace("decode_mdpp_samples: input.size={}, input={:#010x}", dataView.size(), fmt::join(dataView, " "));

    DecodedMdppSampleEvent ret{};

    // Need at least the module header and the EndOfEvent word.
    if (size < 2)
    {
        spdlog::warn("decode_mdpp_samples: input data size < 2, returning null result");
        SAM_ASSERT(!"decode_mdpp_samples: input data size must be >= 2");
        return {};
    }

    // The position of the header and timestamp words is fixed, so we can handle
    // them here, instead of testing each word in the loop.
    if (mvlc::util::matches(filters.fModuleId, data[0]))
    {
        ret.header = data[0];
        ret.headerModuleId = *mvlc::util::extract(filters.fModuleId, data[0], 'D');
    }
    else
    {
        SAM_ASSERT(!"decode_mdpp_samples: fModuleId");
    }

    auto timestamp = extract_timestamp(data, size, filters);

    if (timestamp.has_value())
    {
        ret.timestamp = *timestamp;
    }
    else
    {
        SAM_ASSERT(!"decode_mdpp_samples: timestamp");
    }

    ChannelTrace currentTrace;

	for (auto wordPtr = data+1, dataEnd = data + size; wordPtr < dataEnd; ++wordPtr)
    {
        // The data words containing amplitude or channel time are
        // guaranteed to come before the samples for the respective channel.
        // This means we will always have a valid channel number when starting
        // to process sample data.

        const bool amplitudeMatches = mvlc::util::matches(filters.fAmplitude.filter, *wordPtr);
        const bool channelTimeMatches = mvlc::util::matches(filters.fChannelTime.filter, *wordPtr);

        if (amplitudeMatches || channelTimeMatches)
        {
            auto &theFilter = amplitudeMatches ? filters.fAmplitude : filters.fChannelTime;

            // Note: extract yields unsigned, but the address values do easily
            // fit in signed 32-bit integers.
			s32 addr = *mvlc::util::extract(theFilter, *wordPtr, 'A');
			auto value = *mvlc::util::extract(theFilter, *wordPtr, 'D');

            // TODO: try to compress this code. both branches are so similar
            if (currentTrace.channel >= 0 && currentTrace.channel != addr)
            {
                // The current channel number changed which means we're done
                // with this trace and can prepare for the next one.
                spdlog::trace("decode_mdpp_samples: Finished decoding a channel trace: channel={}, #samples={}, samples={}",
                    currentTrace.channel, currentTrace.samples.size(), fmt::join(currentTrace.samples, ", "));

                ret.traces.push_back(currentTrace); // store the old trace

                // begin the new trace
                reset_trace(currentTrace);
                currentTrace.channel = addr;
                currentTrace.header = ret.header;

                if (amplitudeMatches)
                {
                    currentTrace.amplitude = value;
                    currentTrace.amplitudeData = *wordPtr;
                }
                else
                {
                    currentTrace.time = value;
                    currentTrace.timeData = *wordPtr;
                }
            }
            else
            {
                // We did not have a valid trace before.
                currentTrace.channel = addr;
                currentTrace.header = ret.header;

                if (amplitudeMatches)
                {
                    currentTrace.amplitude = value;
                    currentTrace.amplitudeData = *wordPtr;
                }
                else
                {
                    currentTrace.time = value;
                    currentTrace.timeData = *wordPtr;
                }
            }
        }
		else if (mvlc::util::matches(filters.fSamples.filter, *wordPtr))
		{
            // This error should never happen if the MDPP firmware behaves correctly.
            if (currentTrace.channel < 0)
            {
                spdlog::error("decode_mdpp_samples: got a sample datum without prior amplitude or channel time data.");
                SAM_ASSERT(!"sample datum without prior amplitude or channel time data");
                return {};
            }
            //assert(currentTrace.channel >= 0);

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
#if 1 //#ifndef NDEBUG
        else if (*wordPtr == 0u
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
            spdlog::warn("decode_mdpp_samples: No filter match for word #{}: hex={:#010x}, bin={:#b}",
                std::distance(data, wordPtr), *wordPtr, *wordPtr);
            mvlc::log_buffer(mvlc::default_logger(), spdlog::level::trace, dataView, "raw mdpp sample data");
            //spdlog::warn("decode_mdpp_samples: input.size={}, input={:#010x}", dataView.size(), fmt::join(dataView, " "));
            SAM_ASSERT(!"no filter match in mdpp data");
        }
#endif
    }

    // Handle a possible last trace that was decoded but not yet moved into the
    // result.
    if (currentTrace.channel >= 0)
    {
        spdlog::trace("decode_mdpp_samples: Finished decoding a channel trace: channel={}, #samples={}, samples={}",
            currentTrace.channel, currentTrace.samples.size(), fmt::join(currentTrace.samples, ", "));
        ret.traces.push_back(currentTrace);
    }

    spdlog::trace("decode_mdpp_samples finished decoding: header={:#010x}, timestamp={}, moduleId={:#04x}, #traces={}",
                 ret.header, ret.timestamp, ret.headerModuleId, ret.traces.size());

    return ret;
}

template<typename Filters>
DecodedMdppSampleEvent decode_mdpp_samples_impl_2(const u32 *data, const size_t size, const Filters &filters)
{
    std::basic_string_view<u32> dataView(data, size);

    spdlog::trace("decode_mdpp_samples: input.size={}, input={:#010x}", dataView.size(), fmt::join(dataView, " "));

    DecodedMdppSampleEvent ret{};

    // Need at least the module header and the EndOfEvent word.
    if (size < 2)
    {
        spdlog::warn("decode_mdpp_samples: input data size < 2, returning null result");
        SAM_ASSERT(!"decode_mdpp_samples: input data size must be >= 2");
        return {};
    }

    // The module header word is always the first word. Store it and extract the module ID.
    if (mvlc::util::matches(filters.fModuleId.filter, data[0]))
    {
        ret.header = data[0];
        ret.headerModuleId = *mvlc::util::extract(filters.fModuleId, data[0], 'D');
    }
    else
    {
        SAM_ASSERT(!"decode_mdpp_samples: fModuleId");
    }

    auto timestamp = extract_timestamp(data, size, filters);

    if (timestamp.has_value())
    {
        ret.timestamp = *timestamp;
    }
    else
    {
        SAM_ASSERT(!"decode_mdpp_samples: timestamp");
    }

    return ret;
}

#define ActiveDecoder decode_mdpp_samples_impl

DecodedMdppSampleEvent decode_mdpp16_scp_samples(const u32 *data, const size_t size)
{
    return ActiveDecoder(data, size, mdpp16ScpFilters);
}

DecodedMdppSampleEvent decode_mdpp32_scp_samples(const u32 *data, const size_t size)
{
    return ActiveDecoder(data, size, mdpp32ScpFilters);
}

#undef ActiveDecoder

DecodedMdppSampleEvent LIBMVME_MDPP_DECODE_EXPORT decode_mdpp_samples(const u32 *data, const size_t size, const char *moduleType)
{
    if (strcmp(moduleType, "mdpp16_scp") == 0)
        return decode_mdpp16_scp_samples(data, size);
    else if (strcmp(moduleType, "mdpp32_scp") == 0)
        return decode_mdpp32_scp_samples(data, size);
    else
    {
        spdlog::error("decode_mdpp_samples: unknown module type '{}'", moduleType);
        SAM_ASSERT(!"unknown module type");
        return {};
    }
}

}
