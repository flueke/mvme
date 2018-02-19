#include "rate_monitoring.h"

#include <QBoxLayout>
#include <QDebug>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTimer>
#include <QHeaderView>
#include <QGroupBox>
#include <QSplitter>

#include <qwt_plot_curve.h>
#include <qwt_plot.h>
#include <qwt_plot_legenditem.h>
#include <qwt_scale_engine.h>

#include "analysis/a2/util/nan.h"
#include "histo_util.h"
#include "scrollzoomer.h"
#include "util/assert.h"
#include "mvme_stream_processor.h"
#include "util/counters.h"
#include "sis3153_readout_worker.h"
#include "util/bihash.h"

//
// RateMonitorPlotWidget
//

struct RateMonitorPlotData: public QwtSeriesData<QPointF>
{
    RateMonitorPlotData(const RateHistoryBufferPtr &rateHistory, RateMonitorPlotWidget *plotWidget)
        : QwtSeriesData<QPointF>()
        , rateHistory(rateHistory)
        , plotWidget(plotWidget)
    { }

    size_t size() const override
    {
        return rateHistory->capacity();
    }

    virtual QPointF sample(size_t i) const override
    {
        size_t offset = rateHistory->capacity() - rateHistory->size();
        ssize_t bufferIndex = i - offset;

        double x = 0.0;
        double y = 0.0;

        if (plotWidget->isXAxisReversed())
            x = -(static_cast<double>(rateHistory->capacity()) - i);
        else
            x = i;

        if (0 <= bufferIndex && bufferIndex < static_cast<ssize_t>(rateHistory->size()))
            y = (*rateHistory)[bufferIndex];

        QPointF result(x, y);

#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "sample =" << i
            << ", offset =" << offset
            << ", bufferIndex =" << bufferIndex
            << ", buffer->size =" << buffer->size()
            << ", buffer->cap =" << buffer->capacity()
            << ", result =" << result;
#endif


        return result;
    }

    virtual QRectF boundingRect() const override
    {
        return get_qwt_bounding_rect(*rateHistory);
    }

    RateHistoryBufferPtr rateHistory;
    RateMonitorPlotWidget *plotWidget;
};

RateMonitorPlotData *as_RateMonitorPlotData(QwtSeriesData<QPointF> *data)
{
    return reinterpret_cast<RateMonitorPlotData *>(data);
}

struct RateMonitorPlotWidgetPrivate
{
    QVector<RateHistoryBufferPtr> m_rates;
    bool m_xAxisReversed = false;

    QwtPlot *m_plot;
    ScrollZoomer *m_zoomer;
    QVector<QwtPlotCurve *> m_curves;
    QwtPlotLegendItem m_plotLegendItem;
};

RateMonitorPlotWidget::RateMonitorPlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorPlotWidgetPrivate>())
{
    // plot and curve
    m_d->m_plot = new QwtPlot(this);
    m_d->m_plot->canvas()->setMouseTracking(true);
    m_d->m_plotLegendItem.attach(m_d->m_plot);

    // zoomer
    m_d->m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());
    m_d->m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);

    qDebug() << __PRETTY_FUNCTION__ << "zoomRectIndex =" << m_d->m_zoomer->zoomRectIndex();

    TRY_ASSERT(connect(m_d->m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));
    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &RateMonitorPlotWidget::mouseCursorMovedToPlotCoord));
    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorLeftPlot,
                       this, &RateMonitorPlotWidget::mouseCursorLeftPlot));

    // layout
    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);
    widgetLayout->setSpacing(0);
    widgetLayout->addWidget(m_d->m_plot);

    setYAxisScale(AxisScale::Linear);
}

RateMonitorPlotWidget::~RateMonitorPlotWidget()
{
}

void RateMonitorPlotWidget::addRate(const RateHistoryBufferPtr &rateHistory,
                                    const QString &title,
                                    const QColor &color)
{
    assert(m_d->m_rates.size() == m_d->m_curves.size());

    auto curve = std::make_unique<QwtPlotCurve>(title);
    curve->setData(new RateMonitorPlotData(rateHistory, this));
    curve->setPen(color);
    curve->attach(m_d->m_plot);

    m_d->m_curves.push_back(curve.release());
    m_d->m_rates.push_back(rateHistory);

    assert(m_d->m_rates.size() == m_d->m_curves.size());
}

void RateMonitorPlotWidget::removeRate(const RateHistoryBufferPtr &rateHistory)
{
    removeRate(m_d->m_rates.indexOf(rateHistory));
}

void RateMonitorPlotWidget::removeRate(int index)
{
    assert(m_d->m_rates.size() == m_d->m_curves.size());

    if (0 < index && index < m_d->m_rates.size())
    {
        assert(index < m_d->m_curves.size());

        auto curve = std::unique_ptr<QwtPlotCurve>(m_d->m_curves.at(index));
        curve->detach();
        m_d->m_curves.remove(index);
        m_d->m_rates.remove(index);
    }

    assert(m_d->m_rates.size() == m_d->m_curves.size());
}

int RateMonitorPlotWidget::rateCount() const
{
    assert(m_d->m_rates.size() == m_d->m_curves.size());
    return m_d->m_rates.size();
}

QVector<RateHistoryBufferPtr> RateMonitorPlotWidget::getRates() const
{
    assert(m_d->m_rates.size() == m_d->m_curves.size());
    return m_d->m_rates;
}

RateHistoryBufferPtr RateMonitorPlotWidget::getRate(int index) const
{
    return m_d->m_rates.value(index);
}

bool RateMonitorPlotWidget::isXAxisReversed() const
{
    return m_d->m_xAxisReversed;
}

void RateMonitorPlotWidget::setXAxisReversed(bool b)
{
    m_d->m_xAxisReversed = b;
    m_d->m_zoomer->setZoomBase(false);
    replot();
}

void RateMonitorPlotWidget::replot()
{
    // updateAxisScales
    static const double ScaleFactor = 1.05;

    double xMax = 0.0;
    double yMax = 0.0;

    for (auto &rate: m_d->m_rates)
    {
        xMax = std::max(xMax, static_cast<double>(rate->capacity()));
        yMax = std::max(yMax, get_max_value(*rate));
    }

    double base = 0.0;

    switch (getYAxisScale())
    {
        case AxisScale::Linear:
            base = 0.0;
            yMax *= ScaleFactor;
            break;

        case AxisScale::Logarithmic:
            base = 0.1;
            yMax = std::pow(yMax, ScaleFactor);
            break;
    }

    // This sets a fixed y axis scale effectively overriding any changes made
    // by the scrollzoomer.
    m_d->m_plot->setAxisScale(QwtPlot::yLeft, base, yMax);


    // If fully zoomed out set the x-axis to full resolution
    if (m_d->m_zoomer->zoomRectIndex() == 0)
    {
        if (isXAxisReversed())
            m_d->m_plot->setAxisScale(QwtPlot::xBottom, -xMax, 0.0);
        else
            m_d->m_plot->setAxisScale(QwtPlot::xBottom, 0.0, xMax);

        m_d->m_zoomer->setZoomBase();
    }

    m_d->m_plot->updateAxes();
    m_d->m_plot->replot();
}

static bool axis_is_lin(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLinearScaleEngine *>(plot->axisScaleEngine(axis));
}

static bool axis_is_log(QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<QwtLogScaleEngine *>(plot->axisScaleEngine(axis));
}

void RateMonitorPlotWidget::setYAxisScale(AxisScale scaling)
{
    switch (scaling)
    {
        case AxisScale::Linear:
            m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
            m_d->m_plot->setAxisAutoScale(QwtPlot::yLeft, true);
            break;

        case AxisScale::Logarithmic:
            auto scaleEngine = new QwtLogScaleEngine;
            //scaleEngine->setTransformation(new MinBoundLogTransform);
            m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
            break;
    }

    replot();
}

AxisScale RateMonitorPlotWidget::getYAxisScale() const
{
    if (axis_is_lin(m_d->m_plot, QwtPlot::yLeft))
        return AxisScale::Linear;

    assert(axis_is_log(m_d->m_plot, QwtPlot::yLeft));

    return AxisScale::Logarithmic;
}

void RateMonitorPlotWidget::zoomerZoomed(const QRectF &)
{
    qDebug() << __PRETTY_FUNCTION__ << m_d->m_zoomer->zoomRectIndex();
    replot();
}

void RateMonitorPlotWidget::mouseCursorMovedToPlotCoord(QPointF)
{
}

void RateMonitorPlotWidget::mouseCursorLeftPlot()
{
}

QwtPlot *RateMonitorPlotWidget::getPlot()
{
    return m_d->m_plot;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(const RateHistoryBufferPtr &rate)
{
    int index = m_d->m_rates.indexOf(rate);

    if (0 < index && index < m_d->m_rates.size())
    {
        assert(index < m_d->m_curves.size());
        return m_d->m_curves.at(index);
    }

    return nullptr;
}

QwtPlotCurve *RateMonitorPlotWidget::getPlotCurve(int index)
{
    if (0 < index && index < m_d->m_rates.size())
    {
        assert(index < m_d->m_curves.size());
        return m_d->m_curves.at(index);
    }

    return nullptr;
}

QVector<QwtPlotCurve *> RateMonitorPlotWidget::getPlotCurves()
{
    return m_d->m_curves;
}

//
// RateMonitorWidget
//
struct RateSampler
{
    // state and data
    RateHistoryBufferPtr rateHistory;
    double lastValue = 0.0;

    // setup
    double scaleFactor = 1.0;

    void sample(double value, double dt_s = 1.0)
    {
        double delta = calc_delta0(value, lastValue);
        lastValue = value;

        if (rateHistory)
        {
            double rate = (delta * scaleFactor) / dt_s;
            qDebug() << rateHistory.get() << rate;
            rateHistory->push_back(rate);
        }
    }
};

struct StreamProcessorSampler
{
    using Entry = RateSampler;

    Entry bytesProcessed;
    Entry buffersProcessed;
    Entry buffersWithErrors;
    Entry eventSections;
    Entry invalidEventIndices;

    using ModuleEntries = std::array<Entry, MaxVMEModules>;
    std::array<Entry, MaxVMEEvents> eventEntries;
    std::array<ModuleEntries, MaxVMEEvents> moduleEntries;

    void sample(const MVMEStreamProcessorCounters &counters)
    {
        bytesProcessed.sample(counters.bytesProcessed);
        buffersProcessed.sample(counters.buffersProcessed);
        buffersWithErrors.sample(counters.buffersWithErrors);
        eventSections.sample(counters.eventSections);
        invalidEventIndices.sample(counters.invalidEventIndices);

        for (size_t ei = 0; ei < MaxVMEEvents; ei++)
        {
            eventEntries[ei].sample(counters.eventCounters[ei]);

            for (size_t mi = 0; mi < MaxVMEModules; mi++)
            {
                moduleEntries[ei][mi].sample(counters.moduleCounters[ei][mi]);
            }
        }
    }
};

struct DAQStatsSampler
{
    using Entry = RateSampler;

    Entry totalBytesRead;
    Entry totalBuffersRead;
    Entry buffersWithErrors;
    Entry droppedBuffers;
    Entry totalNetBytesRead;
    Entry listFileBytesWritten;

    void sample(const DAQStats &counters)
    {
        totalBytesRead.sample(counters.totalBytesRead);
        totalBuffersRead.sample(counters.totalBuffersRead);
        buffersWithErrors.sample(counters.buffersWithErrors);
        droppedBuffers.sample(counters.droppedBuffers);
        totalNetBytesRead.sample(counters.totalNetBytesRead);
        listFileBytesWritten.sample(counters.listFileBytesWritten);
    }
};

struct SIS3153Sampler
{
    using Entry = RateSampler;
    using StackListCountEntries = std::array<Entry, SIS3153Constants::NumberOfStackLists>;

    StackListCountEntries stackListCounts;
    StackListCountEntries stackListBerrCounts_Block;
    StackListCountEntries stackListBerrCounts_Read;
    StackListCountEntries stackListBerrCounts_Write;
    Entry lostEvents;
    Entry multiEventPackets;
    StackListCountEntries embeddedEvents;
    StackListCountEntries partialFragments;
    StackListCountEntries reassembledPartials;

    void sample(const SIS3153ReadoutWorker::Counters &counters)
    {
        lostEvents.sample(counters.lostEvents);
        multiEventPackets.sample(counters.multiEventPackets);

        for (size_t i = 0; i < std::tuple_size<StackListCountEntries>::value; i++)
        {
            stackListCounts[i].sample(counters.stackListCounts[i]);
            stackListBerrCounts_Block[i].sample(counters.stackListBerrCounts_Block[i]);
            stackListBerrCounts_Read[i].sample(counters.stackListBerrCounts_Read[i]);
            stackListBerrCounts_Write[i].sample(counters.stackListBerrCounts_Write[i]);
            embeddedEvents[i].sample(counters.embeddedEvents[i]);
            partialFragments[i].sample(counters.partialFragments[i]);
            reassembledPartials[i].sample(counters.reassembledPartials[i]);
        }
    }
};

void RateMonitorWidgetPrivate::dev_test_setup_thingy()
{
    static const size_t RateHistorySampleCapacity = 60 * 5;

    m_streamProcSampler.bytesProcessed.rateHistory = std::make_shared<RateHistoryBuffer>(RateHistorySampleCapacity);
    m_streamProcSampler.bytesProcessed.scaleFactor = 1.0 / Kilobytes(1.0);
    m_plotWidget->addRate(m_streamProcSampler.bytesProcessed.rateHistory, "streamProc.bytes", Qt::black);

    m_streamProcSampler.buffersProcessed.rateHistory = std::make_shared<RateHistoryBuffer>(RateHistorySampleCapacity);
    m_plotWidget->addRate(m_streamProcSampler.buffersProcessed.rateHistory, "streamProc.buffers", Qt::red);
}

void RateMonitorWidgetPrivate::addRateEntryToTable(RateSampler *entry, const QString &name)
{
    assert(!m_rateTableHash.map.contains(entry));

    auto item = new QTableWidgetItem(name);
}

void RateMonitorWidgetPrivate::removeRateEntryFromTable(RateSampler *entry)
{
    assert(!"not implemented");
}

void RateMonitorWidgetPrivate::updateRateTable()
{
    //assert(!"not implemented");
}

RateMonitorWidget::RateMonitorWidget(RateMonitorRegistry *reg, MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorWidgetPrivate>())
{
    setWindowTitle(QSL("Rate Monitor"));

    m_d->m_rateTree = new QTreeWidget;
    m_d->m_rateTable = new QTableWidget;
    m_d->m_plotWidget = new RateMonitorPlotWidget;
    m_d->m_plotWidget->setXAxisReversed(true);
    m_d->m_propertyBox = new QGroupBox("Properties");

    m_d->m_registry = reg;
    m_d->m_context = context;

    // rate tree setup
    {
        auto tree = m_d->m_rateTree;
        tree->header()->setVisible(true);
        tree->setColumnCount(3);
        tree->setHeaderLabels({"Object", "View", "Plot"});
    }

    // rate table setup
    {
        auto table = m_d->m_rateTable;
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({"Name", "Rate", "Value"});
    }

    auto leftVSplitter = new QSplitter(Qt::Vertical);
    leftVSplitter->addWidget(m_d->m_rateTree);
    leftVSplitter->addWidget(m_d->m_propertyBox);
    leftVSplitter->setStretchFactor(0, 1);

    auto rightVSplitter = new QSplitter(Qt::Vertical);
    rightVSplitter->addWidget(m_d->m_rateTable);
    rightVSplitter->addWidget(m_d->m_plotWidget);
    rightVSplitter->setStretchFactor(1, 1);

    auto widgetHSplitter = new QSplitter;
    widgetHSplitter->addWidget(leftVSplitter);
    widgetHSplitter->addWidget(rightVSplitter);
    widgetHSplitter->setStretchFactor(1, 1);

    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->addWidget(widgetHSplitter);

    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &RateMonitorWidget::sample);
    timer->setInterval(1000.0);
    timer->start();

    m_d->dev_test_setup_thingy();
}

RateMonitorWidget::~RateMonitorWidget()
{
}

void RateMonitorWidget::sample()
{
    qDebug() << __PRETTY_FUNCTION__ << "sample";

    m_d->m_daqStatsSampler.sample(m_d->m_context->getDAQStats());
    m_d->m_streamProcSampler.sample(m_d->m_context->getMVMEStreamWorker()->getStreamProcessor()->getCounters());

    if (auto rdoWorker = qobject_cast<SIS3153ReadoutWorker *>(m_d->m_context->getReadoutWorker()))
    {
        m_d->m_sisReadoutSampler.sample(rdoWorker->getCounters());
    }

    m_d->m_plotWidget->replot();
    m_d->updateRateTable();
}
