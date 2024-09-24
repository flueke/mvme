
#include "mdpp_sampling.h"

#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/ticketmutex.h>

#include "analysis/analysis.h"
#include "analysis/a2/a2_support.h"
#include "run_info.h"
#include "util/qt_container.h"
#include "vme_config.h"

using namespace mesytec::mvlc;

namespace mesytec::mvme
{

struct MdppSamplingConsumer::Private
{
    std::shared_ptr<spdlog::logger> logger_;
    Logger qtLogger_;
    mesytec::mvlc::TicketMutex mutex_;
    std::set<QUuid> moduleInterests_;
    vme_analysis_common::VMEIdToIndex vmeIdToIndex_;
    vme_analysis_common::IndexToVmeId indexToVmeId_;

    // This data obtained in beginRun()
    RunInfo runInfo_;
    const VMEConfig *vmeConfig_ = nullptr;
    analysis::Analysis *analysis_ = nullptr;


    bool hasModuleInterest(s32 crateIndex, s32 eventIndex, s32 moduleIndex)
    {
        // TODO: crateIndex needs to be checked if this should be multicrate capable.
        // Map indexes back to the moduleId, then check if it's present in the
        // moduleInterests_ set.
        auto moduleId = indexToVmeId_.value({ eventIndex, moduleIndex});
        return moduleInterests_.find(moduleId) != std::end(moduleInterests_);
    };
};

MdppSamplingConsumer::MdppSamplingConsumer(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->logger_ = get_logger("MdppSamplingConsumer");
    d->logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [tid%t] %v");
    d->logger_->set_level(spdlog::level::debug);
    //d->logger_->debug("MdppSamplingPeriod={}", MdppSamplePeriod.count());
}

MdppSamplingConsumer::~MdppSamplingConsumer()
{
}

void MdppSamplingConsumer::setLogger(Logger logger)
{
    d->qtLogger_ = logger;
}

StreamConsumerBase::Logger &MdppSamplingConsumer::getLogger()
{
    return d->qtLogger_;
}

void MdppSamplingConsumer::beginRun(
    const RunInfo &runInfo, const VMEConfig *vmeConfig, analysis::Analysis *analysis)
{
    d->runInfo_ = runInfo;
    d->vmeConfig_ = vmeConfig;
    d->analysis_ = analysis;
    d->vmeIdToIndex_ = analysis->getVMEIdToIndexMapping();
    d->indexToVmeId_ = reverse_hash(d->vmeIdToIndex_);
}

void MdppSamplingConsumer::endRun(const DAQStats &stats, const std::exception *e)
{
}

void MdppSamplingConsumer::beginEvent(s32 eventIndex)
{
    (void) eventIndex;
}

void MdppSamplingConsumer::endEvent(s32 eventIndex)
{
    (void) eventIndex;
}

void MdppSamplingConsumer::processModuleData(
    s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
    for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        if (d->hasModuleInterest(crateIndex, eventIndex, moduleIndex))
        {
            auto dataBlock = moduleDataList[moduleIndex].data;
            std::vector<u32> buffer(dataBlock.size);
            std::copy(dataBlock.data, dataBlock.data+dataBlock.size, std::begin(buffer));
            vme_analysis_common::VMEConfigIndex vmeIndex{eventIndex, static_cast<s32>(moduleIndex)};
            auto moduleId = d->indexToVmeId_.value(vmeIndex);
            emit moduleDataReady(moduleId, buffer);
        }
    }
}

void MdppSamplingConsumer::processSystemEvent(s32 crateIndex, const u32 *header, u32 size)
{
    (void) crateIndex;
    (void) header;
    (void) size;
}

void MdppSamplingConsumer::processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size)
{
    (void) eventIndex;
    (void) moduleIndex;
    (void) data;
    (void) size;
    assert(!"don't call me please!");
    throw std::runtime_error(fmt::format("{}: don't call me please!", __PRETTY_FUNCTION__));
}

void MdppSamplingConsumer::addModuleInterest(const QUuid &moduleId)
{
    auto guard = std::unique_lock(d->mutex_);
    d->moduleInterests_.insert(moduleId);
}

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

struct ChannelTrace
{
    s32 channel;
    float amplitude;
    float time;
    std::vector<float> samples;
};

struct DecodedMdppSampleEvent
{
    u32 header;
    u64 timestamp;
    std::vector<ChannelTrace> traces;
    u8 moduleId;
};

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

    spdlog::info("decode_mdpp_samples: input={:#010x}", fmt::join(dataView, " "));

    DecodedMdppSampleEvent ret;

    // Need at least the module header and the EndOfEvent word.
    if (size < 2)
        return {};

    // The position of the header and timestamp words is fixed, so we can handle
    // them here, instead of testing each word in the loop.
    if (mvlc::util::matches(fModuleId.filter, data[0]))
    {
        ret.header = data[0];
        ret.moduleId = mvlc::util::extract(fModuleId.dataCache, data[0]);
    }
    if (mvlc::util::matches(fTimeStamp.filter, data[size-1]))
    {
        // 30 low bits of the timestamp
        auto value = mvlc::util::extract(fTimeStamp.dataCache, data[size-1]);
        //spdlog::info("timestamp matched (30 low bits): 0b{:030b}, 0x{:08x}", value, value);
        ret.timestamp |= value;
    }
    else if (size >= 3 && mvlc::util::matches(fExtentedTs.filter, data[size-2]))
    {
        // optional 16 high bits of the extended timestamp if enabled
        auto value = mvlc::util::extract(fExtentedTs.dataCache, data[size-2]);
        //spdlog::info("extended timestamp matched (16 high bits): 0b{:016b}, 0x{:08x}", value, value);
        ret.timestamp |= static_cast<std::uint64_t>(value) << 30;
    }

    ChannelTrace currentTrace;
    currentTrace.channel = -1;

	for (auto wordPtr = data+1, dataEnd = data + size; wordPtr < dataEnd; ++wordPtr)
    {
        // The data words containing amplitude or channel time are
        // guaranteed to come before the samples for the respective channel.
        // This allows us to extract the channel number for the following
        // samples.
		if (mvlc::util::matches(fChannelTime.filter, *wordPtr))
		{
            // Note: extract yields unsigned, but the address values do easily
            // fit in signed 32-bit integers.
			s32 addr = mvlc::util::extract(fChannelTime.addrCache, *wordPtr);
			auto value = mvlc::util::extract(fChannelTime.dataCache, *wordPtr);

            if (currentTrace.channel >= 0 && currentTrace.channel != addr)
            {
                spdlog::info("Finished decoding a channel trace: channel={}, samples={}",
                    currentTrace.channel, fmt::join(currentTrace.samples, ", "));
                ret.traces.emplace_back(std::move(currentTrace));
                currentTrace = {};
                currentTrace.channel = addr;
                currentTrace.time = value;
            }
            else
            {
                currentTrace.channel = addr;
                currentTrace.time = value;
            }
		}
		else if (mvlc::util::matches(fAmplitude.filter, *wordPtr))
		{
			s32 addr = mvlc::util::extract(fAmplitude.addrCache, *wordPtr);
			auto value = mvlc::util::extract(fAmplitude.dataCache, *wordPtr);

            if (currentTrace.channel >= 0 && currentTrace.channel != addr)
            {
                spdlog::info("Finished decoding a channel trace: channel={}, samples={}",
                    currentTrace.channel, fmt::join(currentTrace.samples, ", "));
                ret.traces.emplace_back(std::move(currentTrace));
                currentTrace = {};
                currentTrace.channel = addr;
                currentTrace.amplitude = value;
            }
            else
            {
                currentTrace.channel = addr;
                currentTrace.amplitude = value;
            }
		}
		else if (mvlc::util::matches(fSamples.filter, *wordPtr))
		{
            // This error should never happen if the MDPP firmware behaves correctly.
            if (currentTrace.channel < 0)
                spdlog::error("decode_mdpp_samples: got a sample datum without prior amplitude or channel time data.");
            assert(currentTrace.channel >= 0);

            // The 14 high bits are the even sample, the 14 low bits the odd one.
            constexpr u32 SampleBits = 14;
            constexpr u32 SampleMask = (1u << SampleBits) - 1;

            // Extract the raw sample values
			auto value = mvlc::util::extract(fSamples.dataCache, *wordPtr);
            u32 evenRaw = value & SampleMask;
            u32 oddRaw  = (value >> SampleBits) & SampleMask;

            // Now interpret the 14 bit values as signed and convert to float.
            s64 evenSigned = a2::convert_to_signed(evenRaw, SampleBits);
            s64 oddSigned = a2::convert_to_signed(oddRaw, SampleBits);

            currentTrace.samples.push_back(evenSigned);
            currentTrace.samples.push_back(oddSigned);
        }
    }

    // Handle the last trace that was decoded but not yet moved into the result.
    if (currentTrace.channel >= 0)
    {
        spdlog::info("Finished decoding a channel trace: channel={}, samples={}",
            currentTrace.channel, fmt::join(currentTrace.samples, ", "));
        ret.traces.emplace_back(std::move(currentTrace));
    }

    return ret;
}

struct MdppSamplingUi::Private
{
    // Info provided at construction time
    AnalysisServiceProvider *asp_ = nullptr;
};

MdppSamplingUi::MdppSamplingUi(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->asp_ = asp;
}

MdppSamplingUi::~MdppSamplingUi() { }

void MdppSamplingUi::handleModuleData(const QUuid &moduleId, const std::vector<u32> &buffer)
{
    spdlog::trace("MdppSamplingUi::handleModuleData moduleId={}, size={}",
        moduleId.toString().toLocal8Bit().data(), buffer.size());

    auto samples = decode_mdpp_samples(buffer.data(), buffer.size());

    for (const auto &trace: samples.traces)
    {
        spdlog::info("got a trace for channel {} containing {} samples",
            trace.channel, trace.samples.size());
    }
}

void MdppSamplingUi::addModuleInterest(const QUuid &moduleId)
{
    emit moduleInterestAdded(moduleId);
}

}
