#include "mdpp_sampling_p.h"

#include <cmath>
#include <set>
#include <qwt_symbol.h>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLShader>
#include <QOpenGLTexture>

#include <mesytec-mvlc/cpp_compat.h>
#include <mesytec-mvlc/util/data_filter.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/ticketmutex.h>

#include "analysis_service_provider.h"
#include "analysis/analysis.h"
#include "analysis/a2/a2_support.h"
#include "run_info.h"
#include "util/qt_container.h"
#include "util/qt_logview.h"
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
    std::fill(std::begin(d->linearEventNumbers_), std::end(d->linearEventNumbers_), 0);
    emit sigBeginRun(runInfo, vmeConfig, analysis);
}

void MdppSamplingConsumer::endRun(const DAQStats &stats, const std::exception *e)
{
    emit sigEndRun(stats, e);
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

inline MdppChannelTracePlotData *get_curve_data(QwtPlotCurve *curve)
{
    return reinterpret_cast<MdppChannelTracePlotData *>(curve->data());
}

struct TracePlotWidget::Private
{
    TracePlotWidget *q = nullptr;
    QwtPlotCurve *rawCurve_ = nullptr;                  // curve for the raw samples
    QwtPlotCurve *interpolatedCurve_ = nullptr;         // 2nd curve for the interpolated samples
    MdppChannelTracePlotData *rawCurveData_ = nullptr;     // has to be a ptr as qwt takes ownership and deletes it. more needed for multi plots
    MdppChannelTracePlotData *interpolatedCurveData_ = nullptr;  // ownership goes to qwt when it's attached to
    QwtPlotZoomer *zoomer_ = nullptr;

    void updateAxisScales();
};

void TracePlotWidget::Private::updateAxisScales()
{
    spdlog::trace("entering TracePlotWidget::Private::updateAxisScales()");

    // yAxis
    auto plot = q->getPlot();
    auto br = rawCurveData_->boundingRect();
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
    d->rawCurve_ = new QwtPlotCurve("rawCurve");
    d->interpolatedCurve_ = new QwtPlotCurve("interpolatedCurve");

    for (auto curve: { d->rawCurve_, d->interpolatedCurve_ })
    {
        curve->setPen(Qt::black);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased);
        curve->attach(getPlot());
    }

    // raw samples: no lines, just a marker at each raw sample coordinate
    d->rawCurveData_ = new MdppChannelTracePlotData();
    d->rawCurveData_->setMode(PlotDataMode::Raw);
    d->rawCurve_->setStyle(QwtPlotCurve::NoCurve);
    d->rawCurve_->setData(d->rawCurveData_);

    auto crossSymbol = new QwtSymbol(QwtSymbol::Diamond);
    crossSymbol->setSize(QSize(5, 5));
    crossSymbol->setColor(Qt::red);
    d->rawCurve_->setSymbol(crossSymbol);

    // interpolated samples: lines from point to point, no markers
    d->interpolatedCurveData_ = new MdppChannelTracePlotData();
    d->interpolatedCurveData_->setMode(PlotDataMode::Interpolated);
    d->interpolatedCurve_->setData(d->interpolatedCurveData_);
    d->interpolatedCurve_->setStyle(QwtPlotCurve::Lines);

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

QRectF TracePlotWidget::traceBoundingRect() const
{
    return d->rawCurveData_->boundingRect();
}

QwtPlotCurve *TracePlotWidget::getRawCurve()
{
    return d->rawCurve_;
}

QwtPlotCurve *TracePlotWidget::getInterpolatedCurve()
{
    return d->interpolatedCurve_;
}

void TracePlotWidget::setTrace(const ChannelTrace *trace)
{
    d->rawCurveData_->setTrace(trace);
    d->interpolatedCurveData_->setTrace(trace);
}

const ChannelTrace *TracePlotWidget::getTrace() const
{
    return d->rawCurveData_->getTrace();
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

static const int ReplotInterval_ms = 33;
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
    QPushButton *pb_printInfo_ = nullptr;
    QSpinBox *spin_interpolationFactor_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QCheckBox *cb_sampleSymbols_ = nullptr;
    QCheckBox *cb_interpolatedSymbols_ = nullptr;

    void printInfo();
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
        d->traceSelect_->setMaximum(1);
        d->traceSelect_->setSpecialValueText("latest");
        auto boxStruct = make_vbox_container(QSL("Trace#"), d->traceSelect_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->spin_interpolationFactor_ = new QSpinBox;
        d->spin_interpolationFactor_->setSpecialValueText("off");
        d->spin_interpolationFactor_->setMinimum(0);
        d->spin_interpolationFactor_->setMaximum(100);
        d->spin_interpolationFactor_->setValue(4);
        auto boxStruct = make_vbox_container(QSL("Interpolation Factor"), d->spin_interpolationFactor_, 0, -2);
        tb->addWidget(boxStruct.container.release());
    }

    tb->addSeparator();

    {
        d->cb_sampleSymbols_ = new QCheckBox("Sample Symbols");
        d->cb_sampleSymbols_->setChecked(true);
        d->cb_interpolatedSymbols_ = new QCheckBox("Interpolated Symbols");
        auto [widget, layout] = make_widget_with_layout<QWidget, QVBoxLayout>();
        layout->addWidget(d->cb_sampleSymbols_);
        layout->addWidget(d->cb_interpolatedSymbols_);
        tb->addWidget(widget);
    }

    tb->addSeparator();

    d->pb_printInfo_ = new QPushButton("Print Info");
    tb->addWidget(d->pb_printInfo_);

    connect(d->pb_printInfo_, &QPushButton::clicked, this, [this] { d->printInfo(); });

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
    spdlog::trace("begin MdppSamplingUi::updateUi()");

    // selector 1: Add missing modules to the module select combo
    {
        QSet<QUuid> knownModuleIds;

        for (int i=0; i<d->moduleSelect_->count(); ++i)
        {
            auto moduleId = d->moduleSelect_->itemData(i).value<QUuid>();
            knownModuleIds.insert(moduleId);
        }

        QSet<QUuid> traceModuleIds = QSet<QUuid>::fromList(d->traceHistory_.keys());

        //qDebug() << "knownModuleIds" << knownModuleIds;
        //qDebug() << "traceModuleIds" << traceModuleIds;

        auto missingModuleIds = traceModuleIds.subtract(knownModuleIds);

        //qDebug() << "missingModuleIds" << missingModuleIds;

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

        if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) <= maxChannel)
        {
            auto &tracebuffer = moduleTraceHistory[selectedChannel];
            // selector 3: trace number in the trace history. index 0 is the latest trace.
            d->traceSelect_->setMaximum(std::max(1, tracebuffer.size()-1));
        }
    }
    else // no module selected
    {
        d->channelSelect_->setMaximum(1);
        d->traceSelect_->setMaximum(1);
    }

    // add / remove symbols from / to the curves

    if (auto curve = d->plotWidget_->getRawCurve();
        curve && !d->cb_sampleSymbols_->isChecked())
    {
        curve->setSymbol(nullptr);
    }
    else if (curve && !curve->symbol())
    {
        auto crossSymbol = new QwtSymbol(QwtSymbol::Diamond);
        crossSymbol->setSize(QSize(5, 5));
        crossSymbol->setColor(Qt::red);
        curve->setSymbol(crossSymbol);
    }

    if (auto curve = d->plotWidget_->getInterpolatedCurve();
        curve && !d->cb_interpolatedSymbols_->isChecked())
    {
        curve->setSymbol(nullptr);
    }
    else if (curve && !curve->symbol())
    {
        auto crossSymbol = new QwtSymbol(QwtSymbol::Triangle);
        crossSymbol->setSize(QSize(5, 5));
        crossSymbol->setColor(Qt::blue);
        curve->setSymbol(crossSymbol);
    }

    spdlog::trace("end MdppSamplingUi::updateUi()");
}

void MdppSamplingUi::replot()
{
    spdlog::trace("begin MdppSamplingUi::replot()");
    updateUi(); // update, selection boxes, buttons, etc.

    auto selectedModuleId = d->moduleSelect_->currentData().value<QUuid>();
    ChannelTrace *trace = nullptr;

    if (d->traceHistory_.contains(selectedModuleId))
    {
        auto &moduleTraceHistory = d->traceHistory_[selectedModuleId];
        const auto maxChannel = moduleTraceHistory.size() - 1;
        auto selectedChannel = d->channelSelect_->value();

        if (0 <= selectedChannel && static_cast<size_t>(selectedChannel) <= maxChannel)
        {
            auto &tracebuffer = moduleTraceHistory[selectedChannel];

            // selector 3: trace number in the trace history. 0 is the latest trace.
            auto selectedTraceIndex = d->traceSelect_->value();

            if (0 <= selectedTraceIndex && selectedTraceIndex < tracebuffer.size())
                trace = &tracebuffer[selectedTraceIndex];
        }
    }

    if (trace)
    {
        auto interpolationFactor = d->spin_interpolationFactor_->value();
        trace->interpolated = interpolate(trace->samples, interpolationFactor+1);
    }

    d->plotWidget_->setTrace(trace);
    d->plotWidget_->replot();

    auto sb = getStatusBar();
    sb->clearMessage();

    if (trace)
    {
        auto moduleName = d->asp_->getVMEConfig()->getModuleConfig(trace->moduleId)->getObjectPath();
        auto channel = trace->channel;
        auto traceIndex = d->traceSelect_->value();
        auto boundingRect = d->plotWidget_->traceBoundingRect();
        double yMin = boundingRect.bottom();
        double yMax = boundingRect.top();

        sb->showMessage(QSL("Module: %1, Channel: %2, Trace: %3, Event#: %4, Amplitude=%5, Time=%6, #Samples=%7, yMin=%8, yMax=%9, moduleHeader=0x%10")
            .arg(moduleName).arg(channel).arg(traceIndex).arg(trace->eventNumber)
            .arg(trace->amplitude).arg(trace->time).arg(get_sample_count(*trace))
            .arg(yMin).arg(yMax).arg(trace->header, 8, 16, QLatin1Char('0'))
            );
    }

    spdlog::trace("end MdppSamplingUi::replot()");
}

void MdppSamplingUi::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig, const analysis::Analysis *analysis)
{
    (void) runInfo;
    (void) vmeConfig;
    (void) analysis;
    spdlog::info("MdppSamplingUi::beginRun()");
    d->plotWidget_->setTrace(nullptr);
    d->traceHistory_.clear();
    replot();
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

void MdppSamplingUi::endRun(const DAQStats &stats, const std::exception *e)
{
    (void) stats;
    (void) e;
    spdlog::info("MdppSamplingUi::endRun()");
}

void MdppSamplingUi::addModuleInterest(const QUuid &moduleId)
{
    emit moduleInterestAdded(moduleId);
}

void MdppSamplingUi::Private::printInfo()
{
    auto trace = plotWidget_->getTrace();

    if (!trace)
        return;

    QString text;
    QTextStream out(&text);

    out << fmt::format("sampleCount: {}\n", trace->samples.size()).c_str();
    out << fmt::format("samples: [{}]\n", fmt::join(trace->samples, ", ")).c_str();
    out << fmt::format("interpolated: [{}]\n", fmt::join(trace->interpolated, ", ")).c_str();

    if (!logView_)
    {
        logView_ = make_logview().release();
        logView_->setAttribute(Qt::WA_DeleteOnClose);
        connect(logView_, &QWidget::destroyed, q, [this] { logView_ = nullptr; });
    }

    assert(logView_);
    logView_->setPlainText(text);
    logView_->show();
    logView_->raise();
}

// *************** SINC by r.schneider@mesytec.com ****************************
static double sinc(double phase) // one period runs 0...1, minima at +-0.5
{
#define LIMIT_PERIODES 3.0 // limit sinc function to +-2

    double sinc;

    if ((phase != 0) && (phase >= -LIMIT_PERIODES) && (phase <= LIMIT_PERIODES))
    {
        sinc = ((sin(phase * M_PI)) / (phase * M_PI)) * ((sin(phase * M_PI / LIMIT_PERIODES)) / (phase * M_PI / LIMIT_PERIODES));
    }
    else if (phase == 0)
    {
        sinc = 1;
    }
    else
    {
        sinc = 0;
    }

    return (sinc);
}

// phase= 0...1 (= a2...a3)
static double ipol(double a0, double a1, double a2, double a3, double a4, double a5, double phase) // one period runs 0...1, minima at +-0.5
{
#define LIN_INT 0 // 1 for linear interpolation

    double ipol;
    if (!LIN_INT)
    {
        // phase runs from 0 (position A1) to 1 (position A2)
        ipol = a0 * sinc(-2.0 - phase) + a1 * sinc(-1 - phase) + a2 * sinc(-phase) + a3 * sinc(1.0 - phase) + a4 * sinc(2.0 - phase) + a5 * sinc(3.0 - phase);
    }
    else
        ipol = a2 + (phase * (a3 - a2)); // only linear interpolation

    return (ipol);
}

static const u32 MinInterpolationSamples = 6;

// Note: the first and last few samples are swalloed. MinInterpolationSamples
// samples are needed for interpolation to start. Could use linear interpolation
// for the start and end of the trace.
template <typename Dest>
void interpolate_impl(const std::basic_string_view<s16> &samples, u32 factor, Dest &dest, double dtSample)
{
    spdlog::trace("break!");
    if (factor <= 1 || samples.size() < MinInterpolationSamples)
    {
        size_t x=0;
        std::for_each(std::begin(samples), std::end(samples), [&x, &dest, dtSample](s16 sample)
        {
            dest.push_back(std::make_pair(static_cast<double>(x*dtSample), static_cast<double>(sample)));
            ++x;
        });

        return;
    }

    const size_t WindowMid = (MinInterpolationSamples - 1) / 2;
    const double factor_1 = 1.0 / factor;
    const auto samplesStart = std::begin(samples);
    const auto samplesEnd = std::end(samples);
    auto windowStart = samples.data();
    auto windowEnd = windowStart + MinInterpolationSamples;

    for (auto it=windowStart; it<windowStart+WindowMid; ++it)
    {
        auto x = dtSample * std::distance(std::begin(samples), it);
        dest.push_back(std::make_pair(x, *it));
    }

    while (windowEnd <= samplesEnd)
    {
        assert(std::distance(windowStart, windowEnd) == MinInterpolationSamples);
        util::span<const s16> window(windowStart, MinInterpolationSamples);

        const auto sampleIndex = WindowMid + std::distance(samplesStart, windowStart);
        const double sampleX = sampleIndex * dtSample;
        const double sampleY = samplesStart[sampleIndex];

        dest.push_back(std::make_pair(sampleX, sampleY));

        for (size_t step=0; step<factor-1; ++step)
        {
            double phase = (step+1) * factor_1;
            double y = ipol(
                window[0], window[1], window[2],
                window[3], window[4], window[5], phase);

            auto x = sampleX + phase * dtSample;

            dest.push_back(std::make_pair(x, y));
        }

        // Done with this window, advance both start and end by one.
        ++windowStart;
        ++windowEnd;
    }

    #if 1
    for (auto it=windowStart+WindowMid; it<samplesEnd; ++it)
    {
        auto x = dtSample * std::distance(std::begin(samples), it);
        dest.push_back(std::make_pair(x, *it));
    }
    #endif
}

QVector<std::pair<double, double>> interpolate(const basic_string_view<s16> &samples, u32 factor, double dtSample)
{
    QVector<std::pair<double, double>> result;
    interpolate_impl(samples, factor, result, dtSample);
    return result;
}

}
