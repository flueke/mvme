
#include "mdpp_sampling.h"

#include <set>
#include <qwt_symbol.h>
#include <QSpinBox>

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

void reset_trace(ChannelTrace &trace)
{
    trace.eventNumber = 0;
    trace.moduleId = {};
    trace.amplitude = make_quiet_nan();
    trace.time = make_quiet_nan();
    trace.amplitudeData = 0;
    trace.timeData = 0;
    trace.samples.clear();
}

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

        auto guard = std::unique_lock(mutex_);
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
    (void) stats; (void) e;
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
        ret.headerModuleId = mvlc::util::extract(fModuleId.dataCache, data[0]);
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
                reset_trace(currentTrace);
                currentTrace.channel = addr;

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
            spdlog::warn("decode_mdpp_samples: No filter match for word #{}: {:#010x}!",
                std::distance(data, wordPtr), *wordPtr);
            //assert(!"no filter match in mdpp data");
        }
#endif
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
                 ret.header, ret.timestamp, ret.headerModuleId, ret.traces.size());

    return ret;
}

using TraceBuffer = QList<ChannelTrace>;
using ModuleTraceHistory = QVector<TraceBuffer>;
using TraceHistoryMap = QMap<QUuid, ModuleTraceHistory>;

struct MdppChannelTracePlotData: public QwtSeriesData<QPointF>
{
    const ChannelTrace *trace_ = nullptr;
    QRectF boundingRectCache_;

    //explicit MdppChannelTracePlotData() {}

    // Set the event data to plot
    void setTrace(const ChannelTrace *trace)
    {
         trace_ = trace;
         boundingRectCache_ = {};
    }

    const ChannelTrace *getTrace() const { return trace_; }

    QRectF boundingRect() const override
    {
        if (boundingRectCache_.isValid())
            return boundingRectCache_;

        if (!trace_ || trace_->samples.empty())
            return {};

        auto &samples = trace_->samples;
        auto minMax = std::minmax_element(std::begin(samples), std::end(samples));

        if (minMax.first != std::end(samples) && minMax.second != std::end(samples))
        {
            QPointF topLeft(0, *minMax.second);
            QPointF bottomRight(samples.size(), *minMax.first);
            return QRectF(topLeft, bottomRight);
        }

        return {};
    }

    size_t size() const override
    {
        return trace_ ? trace_->samples.size() : 0;
    }

    QPointF sample(size_t i) const override
    {
        if (trace_ && i < static_cast<size_t>(trace_->samples.size()))
            return QPointF(i, trace_->samples[i]);

        return {};
    }
};

inline MdppChannelTracePlotData *get_curve_data(QwtPlotCurve *curve)
{
    return reinterpret_cast<MdppChannelTracePlotData *>(curve->data());
}

struct MdppSamplingUi::Private
{
    MdppSamplingUi *q = nullptr;
    AnalysisServiceProvider *asp_ = nullptr;
    size_t debugTotalEvents_ = 0;
    QwtPlotCurve *curve_ = nullptr; // main curve . more needed if multiple traces should be plotted.
    DecodedMdppSampleEvent currentEvent_;
    MdppChannelTracePlotData *curveData_ = nullptr; // has to be a ptr as qwt takes ownership and deletes it. more needed for multi plots
    QwtSymbol *sampleCrossSymbol_ = nullptr;
    QwtPlotZoomer *zoomer_ = nullptr;
    QComboBox *moduleSelect_ = nullptr;
    QSpinBox *channelSelect_ = nullptr;
    QSpinBox *eventSelect_ = nullptr;
    TraceHistoryMap traceHistory_;
    QHash<QUuid, size_t> moduleEventHits_; // count of events per module (incremented in handleModuleData())
    size_t traceHistoryMaxDepth = 10;

    void updateAxisScales();
};

MdppSamplingUi::MdppSamplingUi(AnalysisServiceProvider *asp, QWidget *parent)
    : histo_ui::PlotWidget(parent)
    , d(std::make_unique<Private>())
{
    qRegisterMetaType<DecodedMdppSampleEvent>("mesytec::mvme::DecodedMdppSampleEvent");

    d->q = this;
    d->asp_ = asp;
    d->curve_ = new QwtPlotCurve("lineCurve");
    d->curve_->setStyle(QwtPlotCurve::Lines);
    //d->curve_->setCurveAttribute(QwtPlotCurve::Fitted); // TODO: make this a runtime toggle
    d->curve_->setPen(Qt::black);
    d->curve_->setRenderHint(QwtPlotItem::RenderAntialiased);
    d->curve_->attach(getPlot());

    d->curveData_ = new MdppChannelTracePlotData();
    d->curve_->setData(d->curveData_);

    auto crossSymbol = new QwtSymbol(QwtSymbol::Diamond);
    //crossSymbol->setPen(Qt::red);
    crossSymbol->setSize(QSize(5, 5));
    crossSymbol->setColor(Qt::red);

    //crossSymbol->setPinPoint(QPointF(0, 0), true);
    d->curve_->setSymbol(crossSymbol);

    histo_ui::setup_axis_scale_changer(this, QwtPlot::yLeft, "Y-Scale");
    d->zoomer_ = histo_ui::install_scrollzoomer(this);
    histo_ui::install_tracker_picker(this);

    connect(this, &PlotWidget::aboutToReplot, this, [this] { d->updateAxisScales(); });

    // enable both the zoomer and mouse cursor tracker by default

    if (auto zoomAction = findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);

    if (auto trackerPickerAction = findChild<QAction *>("trackerPickerAction"))
        trackerPickerAction->setChecked(true);

    auto tb = getToolBar();
    tb->addSeparator();

    {
        d->moduleSelect_ = new QComboBox;
        auto boxStruct = make_vbox_container(QSL("Module"), d->moduleSelect_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->channelSelect_ = new QSpinBox;
        d->channelSelect_->setMinimum(0);
        d->channelSelect_->setMaximum(0);
        auto boxStruct = make_vbox_container(QSL("Channel"), d->channelSelect_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->eventSelect_ = new QSpinBox;
        d->eventSelect_->setMinimum(0);
        d->eventSelect_->setMaximum(0);
        auto boxStruct = make_vbox_container(QSL("Trace#"), d->eventSelect_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }
}

MdppSamplingUi::~MdppSamplingUi() { }

s32 get_max_channel_number(const DecodedMdppSampleEvent &event)
{
    s32 ret = -1;

    for (const auto &trace: event.traces)
        ret = std::max(ret, trace.channel);

    return ret;
}

s32 get_max_depth(const ModuleTraceHistory &history)
{
    s32 ret = 0;

    for (const auto &traceBuffer: history)
        ret = std::max(ret, traceBuffer.size());

    return ret;
}

void MdppSamplingUi::handleModuleData(const QUuid &moduleId, const std::vector<u32> &buffer)
{
    spdlog::trace("MdppSamplingUi::handleModuleData event#{}, moduleId={}, size={}",
        d->debugTotalEvents_, moduleId.toString().toLocal8Bit().data(), buffer.size());

    auto linearEventNumber = d->moduleEventHits_[moduleId]++; // linear event number specific to this module

    auto decodedEvent = decode_mdpp_samples(buffer.data(), buffer.size());
    decodedEvent.eventNumber = linearEventNumber;
    decodedEvent.moduleId = moduleId;

    for (auto &trace: decodedEvent.traces)
    {
        trace.eventNumber = linearEventNumber;
        trace.moduleId = moduleId;

        spdlog::info("MdppSamplingUi::handleModuleData event#{}, got a trace for channel {} containing {} samples",
                    trace.eventNumber, trace.channel, trace.samples.size());
    }

    // TODO: add this events traces to a per channel tracebuffer to build up a
    // per channel trace history. Limit the history depth to some user specified
    // value.

    // TODO (later): make sure that channels not present in this event do have
    // empty traces appended to their history buffers, otherwise the per channel
    // histories will not line up.

    // TODO: store the events traces in a history buffer for later access.
    d->currentEvent_ = decodedEvent;

    auto eventMaxChannel = get_max_channel_number(decodedEvent);
    auto &eventChannelTraces = decodedEvent.traces;
    auto &moduleTraceHistory = d->traceHistory_[moduleId];
    auto maxHistoryDepth = get_max_depth(moduleTraceHistory);

    if (moduleTraceHistory.size() < eventMaxChannel+1)
    {
        auto oldSize = moduleTraceHistory.size();
        moduleTraceHistory.resize(eventMaxChannel+1);
        for (int i=oldSize; i<moduleTraceHistory.size(); ++i)
        {
            // XXX: leftoff here
        }
    }


#if 0
    if (!d->currentEvent_.traces.empty())
    {
        auto currentTrace = &d->currentEvent_.traces.back(); // XXX: make this selectable

        d->curveData_->setTrace(currentTrace);
        auto boundingRect = d->curveData_->boundingRect();
        qreal x1, y1, x2, y2;
        boundingRect.getCoords(&x1, &y1, &x2, &y2);
        spdlog::info("MdppSamplingUi::handleModuleData event#{}: curve bounding rect: x1={}, y1={}, x2={}, y2={}",
            d->debugTotalEvents_, x1, y1, x2, y2);

        #if 0
        if (d->plotMarkers_.size() < static_cast<size_t>(currentTrace->samples.size()))
            d->plotMarkers_.resize(currentTrace->samples.size());

        for (size_t i = 0; i < static_cast<size_t>(currentTrace->samples.size()); ++i)
        {
            auto &plotMarker = d->plotMarkers_[i];

            if (!plotMarker)
            {
                auto symbol = new QwtSymbol(QwtSymbol::XCross, Qt::red, Qt::SolidLine, QSize(5, 5));
                symbol->setPinPoint(QPointF(0, 0), true);
                //plotMarker = new QwtPlotMarker();
                //plotMarker->setSymbol(symbol);
                //plotMarker->attach(getPlot());
            }

            assert(plotMarker);

            QPointF point(i, currentTrace->samples[i]);
            plotMarker->setValue(point);
        }
        #endif
    }
    //else
    //    d->curveData_->setTrace(nullptr);
#endif

    ++d->debugTotalEvents_;
    replot();
}

void MdppSamplingUi::addModuleInterest(const QUuid &moduleId)
{
    emit moduleInterestAdded(moduleId);
}

void MdppSamplingUi::Private::updateAxisScales()
{
    spdlog::info("entering MdppSamplingUi::Private::updateAxisScales()");

    // yAxis
    auto plot = q->getPlot();
    auto br = curveData_->boundingRect();
    auto minValue = br.bottom();
    auto maxValue = br.top();

    if (histo_ui::is_logarithmic_axis_scale(plot, QwtPlot::yLeft))
    {
        if (minValue <= 0.0)
            minValue = 0.1;

        maxValue = std::max(minValue, maxValue);
    }

    {
        // Scale the y-axis by 5% to have some margin to the top and bottom of the
        // widget. Mostly to make the top scrollbar not overlap the plotted graph.
        minValue *= (minValue < 0.0) ? 1.05 : 0.95;
        maxValue *= (maxValue < 0.0) ? 0.95 : 1.05;
    }

    plot->setAxisScale(QwtPlot::yLeft, minValue, maxValue);

    // xAxis
    if (zoomer_->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        plot->setAxisScale(QwtPlot::xBottom, br.left(), br.right());
        zoomer_->setZoomBase();
    }
}

}
