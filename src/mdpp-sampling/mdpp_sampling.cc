
#include "mdpp_sampling.h"

#include <set>

#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/ticketmutex.h>

#include "analysis/analysis.h"
#include "analysis/a2/a2_support.h"
#include "mvme_qwt.h"
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
    }
};

MdppSamplingConsumer::MdppSamplingConsumer(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->logger_ = get_logger("MdppSamplingConsumer");
    d->logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [tid%t] %v");
    d->logger_->set_level(spdlog::level::debug);
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

    spdlog::info("decode_mdpp_samples: input.size={}, input={:#010x}", dataView.size(), fmt::join(dataView, " "));

    DecodedMdppSampleEvent ret{};

    // Need at least the module header and the EndOfEvent word.
    if (size < 2)
    {
        spdlog::warn("decode_mdpp_samples: input data size < 2, returning null result");
        return {};
    }

    // The position of the header and timestamp words is fixed, so we can handle
    // them here, instead of testing each word in the loop.
    if (mvlc::util::matches(fModuleId.filter, data[0]))
    {
        ret.header = data[0];
        ret.moduleId = mvlc::util::extract(fModuleId.dataCache, data[0]);
    }
    else
    {
        assert(!"decode_mdpp_samples: fModuleId");
    }

    if (mvlc::util::matches(fTimeStamp.filter, data[size-1]))
    {
        // 30 low bits of the timestamp
        auto value = mvlc::util::extract(fTimeStamp.dataCache, data[size-1]);
        //spdlog::info("timestamp matched (30 low bits): 0b{:030b}, 0x{:08x}", value, value);
        ret.timestamp |= value;
    }
    else
    {
        assert(!"decode_mdpp_samples: fTimeStamp");
    }

    if (size >= 3 && mvlc::util::matches(fExtentedTs.filter, data[size-2]))
    {
        // optional 16 high bits of the extended timestamp if enabled
        auto value = mvlc::util::extract(fExtentedTs.dataCache, data[size-2]);
        //spdlog::info("extended timestamp matched (16 high bits): 0b{:016b}, 0x{:08x}", value, value);
        ret.timestamp |= static_cast<std::uint64_t>(value) << 30;
    }
    else
    {
        spdlog::debug("decode_mdpp_samples: no extended timestamp present");
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

            if (currentTrace.channel >= 0 && currentTrace.channel != addr)
            {
                // The current channel number changed which means we're done
                // with this trace and can prepare for the next one.
                spdlog::info("decode_mdpp_samples: Finished decoding a channel trace: channel={}, #samples={}, samples={}",
                    currentTrace.channel, currentTrace.samples.size(), fmt::join(currentTrace.samples, ", "));

                ret.traces.push_back(currentTrace); // store the old trace
                // begin the new trace
                currentTrace = {};
                currentTrace.channel = addr;

                if (amplitudeMatches)
                    currentTrace.amplitude = value;
                else
                    currentTrace.time = value;
            }
            else
            {
                // We did not have a valid trace before.
                currentTrace.channel = addr;
                currentTrace.time = value;
            }
        }
		else if (mvlc::util::matches(fSamples.filter, *wordPtr))
		{
            // This error should never happen if the MDPP firmware behaves correctly.
            if (currentTrace.channel < 0)
            {
                spdlog::error("decode_mdpp_samples: got a sample datum without prior amplitude or channel time data.");
                return {};
            }
            //assert(currentTrace.channel >= 0);

            // The 14 high bits are the even sample, the 14 low bits the odd one.
            constexpr u32 SampleBits = 14;
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
        // TODO: remove this in release builds.
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
            spdlog::warn("decode_mdpp_samples: No filter match for word #{}: {:#010x}!",
                std::distance(data, wordPtr), *wordPtr);
            //assert(!"no filter match in mdpp data");
        }
    }

    // Handle a possible last trace that was decoded but not yet moved into the
    // result.
    if (currentTrace.channel >= 0)
    {
        spdlog::info("decode_mdpp_samples: Finished decoding a channel trace: channel={}, #samples={}, samples={}",
            currentTrace.channel, currentTrace.samples.size(), fmt::join(currentTrace.samples, ", "));
        ret.traces.push_back(currentTrace);
    }

    spdlog::info("decode_mdpp_samples finished decoding: header={:#010x}, timestamp={}, moduleId={:#04x}, #traces={}",
                 ret.header, ret.timestamp, ret.moduleId, ret.traces.size());

    return ret;
}

struct MdppSamplingUi::Private
{
    // Info provided at construction time
    AnalysisServiceProvider *asp_ = nullptr;
    size_t debugTotalEvents_ = 0;
    MdppSamplingPlotWidget *plotWidget_ = nullptr;
};

MdppSamplingUi::MdppSamplingUi(AnalysisServiceProvider *asp, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    qRegisterMetaType<DecodedMdppSampleEvent>("mesytec::mvme::DecodedMdppSampleEvent");

    d->asp_ = asp;
    d->plotWidget_ = new MdppSamplingPlotWidget(this);

    auto l = make_vbox(this);
    l->addWidget(d->plotWidget_);
}

MdppSamplingUi::~MdppSamplingUi() { }

void MdppSamplingUi::handleModuleData(const QUuid &moduleId, const std::vector<u32> &buffer)
{
    spdlog::trace("MdppSamplingUi::handleModuleData event#{}, moduleId={}, size={}",
        d->debugTotalEvents_, moduleId.toString().toLocal8Bit().data(), buffer.size());

    auto decodedEvent = decode_mdpp_samples(buffer.data(), buffer.size());

    for (const auto &trace: decodedEvent.traces)
    {
        spdlog::info("MdppSamplingUi::handleModuleData event#{}, got a trace for channel {} containing {} samples",
                     d->debugTotalEvents_, trace.channel, trace.samples.size());
    }

    d->plotWidget_->addDecodedModuleEvent(decodedEvent);

    ++d->debugTotalEvents_;
}

void MdppSamplingUi::addModuleInterest(const QUuid &moduleId)
{
    emit moduleInterestAdded(moduleId);
}

struct MdppSamplingPlotData: public QwtSeriesData<QPointF>
{
    DecodedMdppSampleEvent event_;
    s32 channel_ = -1;
    double yOffset_ = 0.0;

    MdppSamplingPlotData(const DecodedMdppSampleEvent &event, unsigned channelNumber, double yOffset = 0.0)
        : event_(event)
        , channel_(channelNumber)
        , yOffset_(yOffset)
    {}

    QRectF boundingRect() const override
    {
        // TODO: proper scaling if multiple curves should be shown vertically as
        // in the trigger io DSO widget.
        // x, y, width, height
        return { 0.0, -100, 60, 200 };
    }

    size_t size() const override
    {
        if (0 <= channel_ && channel_ < event_.traces.size())
        {
            return event_.traces[channel_].samples.size();
        }

        return 0;
    }

    QPointF sample(size_t i) const override
    {
        if (0 <= channel_ && channel_ < event_.traces.size())
        {
            auto &channelTrace = event_.traces[channel_];

            if (i < static_cast<size_t>(channelTrace.samples.size()))
            {
                return QPointF(i, channelTrace.samples[i]);
            }
        }

        return {};
    }

    // Set the event data to plot
    void setEvent(const DecodedMdppSampleEvent &event) { event_ = event; }
    const DecodedMdppSampleEvent &getEvent() const { return event_; }

    // Set the channel to plot. nothing will be shown if the channel is not
    // present in the current event.
    void setChannel(s32 channel) { channel_ = channel; }
    s32 getChannel() const { return channel_; }

    double getYOffset() const { return yOffset_; }
    void setYOffset(double y) { yOffset_ = y; }

};

inline MdppSamplingPlotData *get_curve_data(QwtPlotCurve *curve)
{
    return reinterpret_cast<MdppSamplingPlotData *>(curve->data());
}

inline std::unique_ptr<QwtPlotCurve> make_curve(MdppSamplingPlotData *data, const QString &curveName = {})
{
    auto curve = std::make_unique<QwtPlotCurve>(curveName);
    curve->setData(data);
    curve->setStyle(QwtPlotCurve::Lines);
    //curve->setCurveAttribute(QwtPlotCurve::Inverted);
    curve->setPen(Qt::black);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);

    return curve;
}

struct MdppSamplingPlotWidget::Private
{
    MdppSamplingPlotWidget *q;
    QwtPlot *plot_ = nullptr;
    std::vector<QwtPlotCurve *> plotCurves_;
    QLabel *debugLabel_ = nullptr;
};

MdppSamplingPlotWidget::MdppSamplingPlotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    *d = {};
    d->q = this;
    d->plot_ = new QwtPlot();
    d->debugLabel_ = new QLabel("SNAFU");

    auto l = make_vbox(this);
    l->addWidget(d->plot_);
    l->addWidget(d->debugLabel_);
}

MdppSamplingPlotWidget::~MdppSamplingPlotWidget()
{
}

void MdppSamplingPlotWidget::addDecodedModuleEvent(const DecodedMdppSampleEvent &event)
{
    // TODO: think up a scheme for the y offsets. might have to rearrange curves
    // if channels start to respond that where previously silent.

    // TODO: plot the most recent event. show events/s somewhere. force replot (timer or directly?)
    //d->plotData_->setMdppSampleEvent(event);

    std::vector<s32> missingChannels;

    // Create missing plots, update sample data of existing plots.
    for (const auto &channelTrace: event.traces)
    {
        auto pred = [&event, &channelTrace] (QwtPlotCurve *curve)
        {
            if (auto data = get_curve_data(curve))
            {
                const auto &curveEvent = data->getEvent();
                const auto &curveChannel = data->getChannel();
                return (event.vmeConfigModuleId == curveEvent.vmeConfigModuleId
                        && channelTrace.channel == curveChannel);
            }

            return false;
        };

        if (auto it = std::find_if(std::begin(d->plotCurves_), std::end(d->plotCurves_), pred);
            it != std::end(d->plotCurves_))
        {
            if (auto data = get_curve_data(*it))
            {
                assert(data->getChannel() == channelTrace.channel);
                assert(data->getEvent().vmeConfigModuleId == event.vmeConfigModuleId);
                data->setEvent(event);
            }
        }
        else
        {
            missingChannels.push_back(channelTrace.channel);
        }
    }

    for (auto channelNumber: missingChannels)
    {
        auto data = new MdppSamplingPlotData(event, channelNumber, 0.0);
        auto curveName = QSL("vmeModuleId={}, channel={}").arg(event.vmeConfigModuleId.toString()).arg(channelNumber);
        auto curve = make_curve(data, curveName);
        curve->attach(d->plot_);
        d->plotCurves_.push_back(curve.release());
    }

    d->plot_->replot();
}

}
