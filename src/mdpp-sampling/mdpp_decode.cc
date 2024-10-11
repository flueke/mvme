#include "mdpp_decode.h"

#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/logging.h>

#include "util/math.h"
#include "analysis/a2/a2_support.h"

#if 0
#define SAM_ASSERT(cond) assert(cond)
#else
#define SAM_ASSERT(cond)
#endif

namespace mesytec::mvme
{

namespace
{

struct FilterWithCache
{
	mvlc::util::DataFilter filter;
	mvlc::util::CacheEntry addrCache; // for the 'A' marker to extract address values
	mvlc::util::CacheEntry dataCache; // for the 'D" marker to extract data values
};

FilterWithCache make_filter(const std::string &pattern)
{
	FilterWithCache result;
	result.filter = mvlc::util::make_filter(pattern);
	result.addrCache = mvlc::util::make_cache_entry(result.filter, 'A');
	result.dataCache = mvlc::util::make_cache_entry(result.filter, 'D');
	return result;
}

}

void reset_trace(ChannelTrace &trace)
{
    trace.eventNumber = 0;
    trace.moduleId = QUuid();
    trace.amplitude = ::mvme::util::make_quiet_nan();
    trace.time = ::mvme::util::make_quiet_nan();
    trace.amplitudeData = 0;
    trace.timeData = 0;
    trace.samples.clear();
}

DecodedMdppSampleEvent decode_mdpp_samples(const u32 *data, const size_t size)
{
	static FilterWithCache fModuleId       = make_filter("0100 XXXX DDDD DDDD XXXX XXXX XXXX XXXX");
	static FilterWithCache fChannelTime    = make_filter("0001 XXXP O01A AAAA DDDD DDDD DDDD DDDD");
	static FilterWithCache fAmplitude      = make_filter("0001 XXXP O00A AAAA DDDD DDDD DDDD DDDD");
	static FilterWithCache fTriggerTime    = make_filter("0001 XXXX X100 000A DDDD DDDD DDDD DDDD");
	static FilterWithCache fTimeStamp      = make_filter("11DD DDDD DDDD DDDD DDDD DDDD DDDD DDDD");
	static FilterWithCache fExtentedTs     = make_filter("0010 XXXX XXXX XXXX DDDD DDDD DDDD DDDD");
	static FilterWithCache fSamples        = make_filter("0011 DDDD DDDD DDDD DDDD DDDD DDDD DDDD");

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
    if (mvlc::util::matches(fModuleId.filter, data[0]))
    {
        ret.header = data[0];
        ret.headerModuleId = mvlc::util::extract(fModuleId.dataCache, data[0]);
    }
    else
    {
        SAM_ASSERT(!"decode_mdpp_samples: fModuleId");
    }

    if (mvlc::util::matches(fTimeStamp.filter, data[size-1]))
    {
        // 30 low bits of the timestamp
        auto value = mvlc::util::extract(fTimeStamp.dataCache, data[size-1]);
        //spdlog::trace("timestamp matched (30 low bits): 0b{:030b}, 0x{:08x}", value, value);
        ret.timestamp |= value;
    }
    else
    {
        SAM_ASSERT(!"decode_mdpp_samples: fTimeStamp");
    }

    if (size >= 3 && mvlc::util::matches(fExtentedTs.filter, data[size-2]))
    {
        // optional 16 high bits of the extended timestamp if enabled
        auto value = mvlc::util::extract(fExtentedTs.dataCache, data[size-2]);
        //spdlog::trace("extended timestamp matched (16 high bits): 0b{:016b}, 0x{:08x}", value, value);
        ret.timestamp |= static_cast<std::uint64_t>(value) << 30;
    }
    else
    {
        spdlog::trace("decode_mdpp_samples: no extended timestamp present");
    }

    ChannelTrace currentTrace;

	for (auto wordPtr = data+1, dataEnd = data + size; wordPtr < dataEnd; ++wordPtr)
    {
        // The data words containing amplitude or channel time are
        // guaranteed to come before the samples for the respective channel.
        // This means we will always have a valid channel number when starting
        // to process sample data.

        const bool amplitudeMatches = mvlc::util::matches(fAmplitude.filter, *wordPtr);
        const bool channelTimeMatches = mvlc::util::matches(fChannelTime.filter, *wordPtr);

        if (amplitudeMatches || channelTimeMatches)
        {
            auto &theFilter = amplitudeMatches ? fAmplitude : fChannelTime;

            // Note: extract yields unsigned, but the address values do easily
            // fit in signed 32-bit integers.
			s32 addr = mvlc::util::extract(theFilter.addrCache, *wordPtr);
			auto value = mvlc::util::extract(theFilter.dataCache, *wordPtr);

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
		else if (mvlc::util::matches(fSamples.filter, *wordPtr))
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
			auto value = mvlc::util::extract(fSamples.dataCache, *wordPtr);
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
                || mvlc::util::matches(fTimeStamp.filter, *wordPtr)
                || mvlc::util::matches(fExtentedTs.filter, *wordPtr)
                || mvlc::util::matches(fTriggerTime.filter, *wordPtr))
        {
            // Explicit test for fillword, timestamp and other known data words
            // to avoid issuing unneeded warnings.
        }
        else
        {
            // Hit an unexpected data word.
            spdlog::warn("decode_mdpp_samples: No filter match for word #{}: {:#010x}",
                std::distance(data, wordPtr), *wordPtr);
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

}
