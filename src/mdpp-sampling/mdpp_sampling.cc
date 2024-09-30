#include "mdpp_sampling_p.h"

#include <set>
#include <qwt_symbol.h>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLShader>
#include <QOpenGLTexture>

#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/ticketmutex.h>

#include "analysis_service_provider.h"
#include "analysis/analysis.h"
#include "analysis/a2/a2_support.h"
#include "run_info.h"
#include "util/qt_container.h"
#include "vme_config.h"

using namespace mesytec::mvlc;

#if 0
#define SAM_ASSERT(cond) assert(cond)
#else
#define SAM_ASSERT(cond)
#endif

namespace mesytec::mvme
{

void reset_trace(ChannelTrace &trace)
{
    trace.eventNumber = 0;
    trace.moduleId = QUuid();
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
    std::array<size_t, MaxVMEEvents> linearEventNumbers_;

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
    d->linearEventNumbers_.fill(0);
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
    assert(0 <= eventIndex && static_cast<u32>(eventIndex) < d->linearEventNumbers_.size());

    ++d->linearEventNumbers_[eventIndex];

    for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        if (d->hasModuleInterest(crateIndex, eventIndex, moduleIndex))
        {
            auto dataBlock = moduleDataList[moduleIndex].data;
            std::vector<u32> buffer(dataBlock.size);
            std::copy(dataBlock.data, dataBlock.data+dataBlock.size, std::begin(buffer));
            vme_analysis_common::VMEConfigIndex vmeIndex{eventIndex, static_cast<s32>(moduleIndex)};
            auto moduleId = d->indexToVmeId_.value(vmeIndex);
            emit moduleDataReady(moduleId, buffer, d->linearEventNumbers_[eventIndex]);
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


inline MdppChannelTracePlotData *get_curve_data(QwtPlotCurve *curve)
{
    return reinterpret_cast<MdppChannelTracePlotData *>(curve->data());
}

struct TracePlotWidget::Private
{
    TracePlotWidget *q = nullptr;
    QwtPlotCurve *curve_ = nullptr; // main curve . more needed if multiple traces should be plotted.
    MdppChannelTracePlotData *curveData_ = nullptr; // has to be a ptr as qwt takes ownership and deletes it. more needed for multi plots
    QwtPlotZoomer *zoomer_ = nullptr;

    void updateAxisScales();
};

void TracePlotWidget::Private::updateAxisScales()
{
    spdlog::trace("entering TracePlotWidget::Private::updateAxisScales()");

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

TracePlotWidget::TracePlotWidget(QWidget *parent)
    : histo_ui::PlotWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
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
    DO_AND_ASSERT(connect(d->zoomer_, SIGNAL(zoomed(const QRectF &)), this, SLOT(replot())));

    // enable both the zoomer and mouse cursor tracker by default

    if (auto zoomAction = findChild<QAction *>("zoomAction"))
        zoomAction->setChecked(true);

    if (auto trackerPickerAction = findChild<QAction *>("trackerPickerAction"))
        trackerPickerAction->setChecked(true);

    auto tb = getToolBar();
    tb->addSeparator();

}

TracePlotWidget::~TracePlotWidget()
{
}

void TracePlotWidget::setTrace(const ChannelTrace *trace)
{
    d->curveData_->setTrace(trace);
}

struct GlTracePlotWidget::Private
{
    GlTracePlotWidget *q = nullptr;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;
    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLShader> m_vertextShader;
    std::unique_ptr<QOpenGLShader> m_fragmentShader;
    std::unique_ptr<QOpenGLTexture> m_texture;

    ~Private()
    {
        q->makeCurrent();
        m_texture = {};
        m_vertextShader = {};
        m_fragmentShader = {};
        m_program = {};

        m_vao.destroy();
        m_vbo.destroy();

        q->doneCurrent();
    };

    static float vertices[];
};

float GlTracePlotWidget::Private::vertices[] = {
    -0.5f, -0.5f, 0.0f,
    0.5f, -0.5f, 0.0f,
    0.0f, 0.5f, 0.0f};

GlTracePlotWidget::GlTracePlotWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    connect(this, &GlTracePlotWidget::aboutToCompose, [&] { spdlog::debug("{}: aboutToCompose", fmt::ptr(this)); });
    connect(this, &GlTracePlotWidget::frameSwapped, [&] { spdlog::debug("{} frameSwapped", fmt::ptr(this)); });
    connect(this, &GlTracePlotWidget::aboutToResize, [&] { spdlog::debug("{}: aboutToResize", fmt::ptr(this)); });
    connect(this, &GlTracePlotWidget::resized, [&] { spdlog::debug("{}: resized", fmt::ptr(this)); });
}

GlTracePlotWidget::~GlTracePlotWidget()
{
}

#if 0
void GlTracePlotWidget::setTrace(const ChannelTrace *trace)
{
}
#endif

void GlTracePlotWidget::setTrace(const float *samples, size_t size)
{
    makeCurrent();

    if (!d->m_vbo.bind())
        assert(!"!vbo.bind()");

    d->m_vbo.allocate(samples, size * sizeof(*samples));
    d->m_vbo.release();

    if (!d->m_program->bind())
        assert(!"!program.bind()");
    //d->m_program->enableAttributeArray(0);
    //d->m_program->setAttributeBuffer(0, GL_FLOAT, GL_FALSE, 1, 0);
}

void GlTracePlotWidget::initializeGL()
{
    spdlog::debug("{}: GlTracePlotWidget::initializeGL()", fmt::ptr(this));
    // Set up the rendering context, load shaders and other resources, etc.:
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(0.2f, 0.2f, 0.3f, 1.0f);

    d->m_vbo.create();
    d->m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    d->m_vbo.bind();
    //d->m_vbo.allocate(d->vertices, sizeof(d->vertices) * sizeof(*d->vertices));

    if (!d->m_vao.create())
        assert(!"!vao.create()");

    d->m_vao.bind();

    d->m_program = std::make_unique<QOpenGLShaderProgram>();

    if (!d->m_program->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, ":/mdpp-sampling/vertex_shader0.glsl"))
        spdlog::error("Error compiling vertex shader");

    if (!d->m_program->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, ":/mdpp-sampling/fragment_shader0.glsl"))
        spdlog::error("Error compiling fragment shader");

    if (!d->m_program->link())
        spdlog::error("Error linking shader program");

    if (!d->m_program->bind())
        spdlog::error("Error binding shader program");

    //d->m_program->enableAttributeArray(0);
    d->m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));

    d->m_vbo.release();
    d->m_vao.release();
}

void GlTracePlotWidget::resizeGL(int w, int h)
{
    spdlog::debug("{}: GlTracePlotWidget::resizeGL({}, {})", fmt::ptr(this), w, h);
    // Update projection matrix and other size related settings:
    //m_projection.setToIdentity();
    //m_projection.perspective(45.0f, w / float(h), 0.01f, 100.0f);
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glViewport(0, 0, w, h);
}

void GlTracePlotWidget::paintGL()
{
    spdlog::debug("{}: GlTracePlotWidget::paintGL()", fmt::ptr(this));
    // Draw the scene:
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClear(GL_COLOR_BUFFER_BIT);

    //d->m_vbo.bind();
    //d->m_vbo.allocate(d->vertices, sizeof(d->vertices) * sizeof(*d->vertices));
    //d->m_vbo.release();

    d->m_program->bind();
    //d->m_vao.bind();
    f->glDrawArrays(GL_TRIANGLES, 0, 3);
    //f->glDrawArrays(GL_POINTS, 0, 256);
    d->m_vao.release();

    #if 0

    int aPosLocation = d->m_program->attributeLocation("aPos");
    //int fragColorLocation = d->m_program->attributeLocation("FragColor");
    assert(aPosLocation == 0);

    d->m_program->setAttributeArray(aPosLocation, d->vertices, 3);
    d->m_program->enableAttributeArray(aPosLocation);
    f->glDrawArrays(GL_TRIANGLES, 0, 3);
    d->m_program->disableAttributeArray(aPosLocation);
    //assert(fragColorLocation >= 0);
    #elif 0
    int vertexLocation = d->m_program->attributeLocation("vertex");
    int matrixLocation = d->m_program->uniformLocation("matrix");
    int colorLocation = d->m_program->uniformLocation("color");

    static GLfloat const triangleVertices[] = {
    60.0f,  10.0f,  0.0f,
    110.0f, 110.0f, 0.0f,
    10.0f,  110.0f, 0.0f
    };

    QColor color(0, 255, 0, 255);

    QMatrix4x4 pmvMatrix;
    pmvMatrix.ortho(rect());

    d->m_program->enableAttributeArray(vertexLocation);
    d->m_program->setAttributeArray(vertexLocation, triangleVertices, 3);
    d->m_program->setUniformValue(matrixLocation, pmvMatrix);
    d->m_program->setUniformValue(colorLocation, color);


    d->m_program->disableAttributeArray(vertexLocation);
    #endif
}

static const int ReplotInterval_ms = 500;
static const size_t TraceHistoryMaxDepth = 10 * 1000;

struct MdppSamplingUi::Private
{
    MdppSamplingUi *q = nullptr;
    AnalysisServiceProvider *asp_ = nullptr;
    TracePlotWidget *plotWidget_ = nullptr;
    DecodedMdppSampleEvent currentEvent_;
    QComboBox *moduleSelect_ = nullptr;
    QSpinBox *channelSelect_ = nullptr;
    QSpinBox *traceSelect_ = nullptr;
    TraceHistoryMap traceHistory_;
    size_t traceHistoryMaxDepth = 10;
    QTimer replotTimer_;
    QPushButton *pb_replot_ = nullptr;
    QPushButton *pb_updateUi_ = nullptr;
};

MdppSamplingUi::MdppSamplingUi(AnalysisServiceProvider *asp, QWidget *parent)
    : histo_ui::IPlotWidget(parent)
    , d(std::make_unique<Private>())
{
    qRegisterMetaType<DecodedMdppSampleEvent>("mesytec::mvme::DecodedMdppSampleEvent");
    qRegisterMetaType<size_t>("size_t");

    setWindowTitle("MDPP Sampling Mode Trace Browser");

    d->q = this;
    d->asp_ = asp;
    d->plotWidget_ = new TracePlotWidget(this);
    d->replotTimer_.setInterval(ReplotInterval_ms);
    connect(&d->replotTimer_, &QTimer::timeout, this, &MdppSamplingUi::replot);
    d->replotTimer_.start();

    auto l = make_hbox<0, 0>(this);
    l->addWidget(d->plotWidget_);

    auto tb = getToolBar();

    {
        d->moduleSelect_ = new QComboBox;
        d->moduleSelect_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
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
        d->traceSelect_ = new QSpinBox;
        d->traceSelect_->setMinimum(0);
        d->traceSelect_->setMaximum(0);
        d->traceSelect_->setSpecialValueText("latest");
        auto boxStruct = make_vbox_container(QSL("Trace#"), d->traceSelect_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->pb_replot_ = new QPushButton("Replot");
        d->pb_updateUi_ = new QPushButton("Update UI");

        tb->addWidget(d->pb_replot_);
        tb->addWidget(d->pb_updateUi_);
    }

    connect(d->pb_replot_, &QPushButton::clicked, this, &MdppSamplingUi::replot);
    connect(d->pb_updateUi_, &QPushButton::clicked, this, &MdppSamplingUi::updateUi);

    connect(d->moduleSelect_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MdppSamplingUi::replot);
    connect(d->channelSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &MdppSamplingUi::replot);
    connect(d->traceSelect_, qOverload<int>(&QSpinBox::valueChanged), this, &MdppSamplingUi::replot);
}

MdppSamplingUi::~MdppSamplingUi() { }

QwtPlot *MdppSamplingUi::getPlot()
{
    return d->plotWidget_->getPlot();
}

const QwtPlot *MdppSamplingUi::getPlot() const
{
    return d->plotWidget_->getPlot();
}

QToolBar *MdppSamplingUi::getToolBar()
{
    return d->plotWidget_->getToolBar();
}

QStatusBar *MdppSamplingUi::getStatusBar()
{
    return d->plotWidget_->getStatusBar();
}

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

void MdppSamplingUi::updateUi()
{
    spdlog::info("begin MdppSamplingUi::updateUi()");

    // selector 1: Add missing modules to the module select combo
    {
        QSet<QUuid> knownModuleIds;

        for (int i=0; i<d->moduleSelect_->count(); ++i)
        {
            auto moduleId = d->moduleSelect_->itemData(i).value<QUuid>();
            knownModuleIds.insert(moduleId);
        }

        QSet<QUuid> traceModuleIds = QSet<QUuid>::fromList(d->traceHistory_.keys());

        auto missingModuleIds = traceModuleIds.subtract(knownModuleIds);

        qDebug() << "knownModuleIds" << knownModuleIds;
        qDebug() << "traceModuleIds" << traceModuleIds;
        qDebug() << "missingModuleIds" << missingModuleIds;

        for (auto moduleId: missingModuleIds)
        {
            QString moduleName;

            if (auto moduleConfig = d->asp_->getVMEConfig()->getModuleConfig(moduleId))
                moduleName = moduleConfig->getObjectPath();
            else
                moduleName = moduleId.toString();

            d->moduleSelect_->addItem(moduleName, moduleId);
        }
    }

    // selector 2: Update the channel number spinbox
    if (auto selectedModuleId = d->moduleSelect_->currentData().value<QUuid>();
        !selectedModuleId.isNull())
    {
        auto &moduleTraceHistory = d->traceHistory_[selectedModuleId];
        const auto maxChannel = moduleTraceHistory.size() - 1;
        d->channelSelect_->setMaximum(maxChannel);
        auto selectedChannel = d->channelSelect_->value();

        if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) < maxChannel)
        {
            auto &tracebuffer = moduleTraceHistory[selectedChannel];
            // selector 3: trace number in the trace history. index 0 is the latest trace.
            d->traceSelect_->setMaximum(tracebuffer.size()-1);
        }
    }
    else // no module selected
    {
        d->channelSelect_->setMaximum(0);
        d->traceSelect_->setMaximum(0);
        d->plotWidget_->setTrace(nullptr);
    }
    spdlog::info("end MdppSamplingUi::updateUi()");
}

void MdppSamplingUi::replot()
{
    spdlog::info("begin MdppSamplingUi::replot()");
    updateUi(); // TODO: decouple updateUi() and replot()?

    auto selectedModuleId = d->moduleSelect_->currentData().value<QUuid>();
    ChannelTrace *trace = nullptr;

    if (d->traceHistory_.contains(selectedModuleId))
    {
        auto &moduleTraceHistory = d->traceHistory_[selectedModuleId];
        const auto maxChannel = moduleTraceHistory.size();
        auto selectedChannel = d->channelSelect_->value();

        if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) < maxChannel)
        {
            auto &tracebuffer = moduleTraceHistory[selectedChannel];

            // selector 3: trace number in the trace history. 0 is the latest trace.
            auto selectedTraceIndex = d->traceSelect_->value();

            if (0 <= selectedTraceIndex && selectedTraceIndex < tracebuffer.size())
                trace = &tracebuffer[selectedTraceIndex];
        }
    }

    auto sb = getStatusBar();

    if (trace)
    {
        // Could use 'auto moduleName = d->moduleSelect_->currentText();' but
        // instead going through the lookup again using trace->moduleId.
        auto moduleName = d->asp_->getVMEConfig()->getModuleConfig(trace->moduleId)->getObjectPath();
        auto channel = trace->channel;
        auto traceIndex = d->traceSelect_->value();
        double yMin = make_quiet_nan();
        double yMax = make_quiet_nan();
        auto minMax = std::minmax_element(std::begin(trace->samples), std::end(trace->samples));

        if (minMax.first != std::end(trace->samples))
            yMin = *minMax.first;

        if (minMax.second != std::end(trace->samples))
            yMax = *minMax.second;

        sb->showMessage(QSL("Module: %1, Channel: %2, Trace: %3, Event#: %4, Amplitude=%5, Time=%6, #Samples=%7, yMin=%8, yMax=%9, moduleHeader=0x%10")
            .arg(moduleName).arg(channel).arg(traceIndex).arg(trace->eventNumber)
            .arg(trace->amplitude).arg(trace->time).arg(trace->samples.size())
            .arg(yMin).arg(yMax).arg(trace->header, 8, 16, QLatin1Char('0'))
            );
    }
    else
    {
        sb->clearMessage();
    }
    d->plotWidget_->setTrace(trace);
    d->plotWidget_->replot();
    spdlog::info("end MdppSamplingUi::replot()");
}

void MdppSamplingUi::handleModuleData(const QUuid &moduleId, const std::vector<u32> &buffer, size_t linearEventNumber)
{
    spdlog::trace("MdppSamplingUi::handleModuleData event#{}, moduleId={}, size={}",
        linearEventNumber, moduleId.toString().toLocal8Bit().data(), buffer.size());

    auto decodedEvent = decode_mdpp_samples(buffer.data(), buffer.size());
    decodedEvent.eventNumber = linearEventNumber;
    decodedEvent.moduleId = moduleId;

    for (auto &trace: decodedEvent.traces)
    {
        // fill in missing info (decode_mdpp_samples() does not know this stuff)
        trace.eventNumber = linearEventNumber;
        trace.moduleId = moduleId;
    }

    for (const auto &trace: decodedEvent.traces)
    {
        spdlog::trace("MdppSamplingUi::handleModuleData event#{}, got a trace for channel {} containing {} samples",
                    trace.eventNumber, trace.channel, trace.samples.size());
    }

    // Add the events traces to the trace history.

    auto it_maxChan = std::max_element(std::begin(decodedEvent.traces), std::end(decodedEvent.traces),
        [](const auto &lhs, const auto &rhs) { return lhs.channel < rhs.channel; });

    // No traces in the event, nothing to do.
    if (it_maxChan == std::end(decodedEvent.traces))
        return;

    auto &moduleTraceHistory = d->traceHistory_[moduleId];
    moduleTraceHistory.resize(std::max(moduleTraceHistory.size(), static_cast<size_t>(it_maxChan->channel)+1));

    for (const auto &trace: decodedEvent.traces)
    {
        auto &traceBuffer = moduleTraceHistory[trace.channel];
        traceBuffer.push_front(trace); // prepend the trace -> newest trace is always at the front
        // remove old traces
        while (static_cast<size_t>(traceBuffer.size()) > TraceHistoryMaxDepth)
            traceBuffer.pop_back();
    }
}

void MdppSamplingUi::addModuleInterest(const QUuid &moduleId)
{
    emit moduleInterestAdded(moduleId);
}

}
