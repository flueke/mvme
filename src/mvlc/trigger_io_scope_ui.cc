#include "mvlc/trigger_io_scope_ui.h"

#include <chrono>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QtConcurrent>
#include <QTimer>
#include <QStandardItemModel>
#include <QStandardItem>
#include <iterator>
#include <qnamespace.h>

#include "mesytec-mvlc/mvlc_error.h"
#include "mesytec-mvlc/util/threadsafequeue.h"
#include "mesytec-mvlc/vme_constants.h"
#include "mvlc/mvlc_trigger_io.h"
#include "mvme_qwt.h"
#include "qt_util.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io_scope
{

// Data provider for QwtPlotCurve.
//
// Timeline contains the samples to plot, yOffset is used to draw multiple
// traces in the same plot at different y coordinates, the preTriggerTime is
// used for correct x-axis scaling: time values in the range (0,
// preTriggerTime) are mapped to (-preTriggerTime, 0) so that the trigger is
// always at 0.
//
// When a ScopeData* is set on a curve via QwtPlotCurve::setData the curve
// takes ownership.
struct ScopeData: public QwtSeriesData<QPointF>
{
    ScopeData(
        const Timeline &timeline,
        const double yOffset
        )
        : timeline(timeline)
        , yOffset(yOffset)
    {
        //qDebug() << __PRETTY_FUNCTION__  << this;
    }

    ~ScopeData() override
    {
        qDebug() << __PRETTY_FUNCTION__  << this;
    }

    QRectF boundingRect() const override
    {
        if (!timeline.empty())
        {
            double tMin = timeline.empty() ? 0 : timeline.front().time.count();
            double tMax = timeline.empty() ? 0 : timeline.back().time.count();
            double tRange = tMax - tMin;
            auto result = QRectF(tMin, yOffset, tRange, 1.0);
            //qDebug() << __PRETTY_FUNCTION__ << "result=" << result;
            return result;
        }

        return {};
    }

    size_t size() const override
    {
        return timeline.size();
    }

    QPointF sample(size_t i) const override
    {
        if (i < timeline.size())
        {
            double time = timeline[i].time.count();
            //qDebug() << __PRETTY_FUNCTION__ << time;
            double value = static_cast<double>(timeline[i].edge);
            return { time, value + yOffset };
        }

        return {};
    }

    trigger_io_scope::Timeline timeline;
    double yOffset;

};

class ScopeScaleDraw: public QwtScaleDraw
{
    public:
        ~ScopeScaleDraw() override
        {
            qDebug() << __PRETTY_FUNCTION__ << this;
        }

        QwtText label(double value) const override
        {
            auto it = std::find_if(
                std::begin(m_data), std::end(m_data),
                [value] (const auto &entry)
                {
                    return entry.first.contains(value);
                });

            if (it != std::end(m_data))
                return { it->second + " (y=" + QwtScaleDraw::label(value).text() + ")" };

            return QwtScaleDraw::label(value);
        }

        void addScaleEntry(double yOffset, const QString &label)
        {
            m_data.push_back(std::make_pair(QwtInterval(yOffset, yOffset + 1.0), label));
        }

        void clear()
        {
            m_data.clear();
        }

    private:
        using Entry = std::pair<QwtInterval, QString>;

        std::vector<Entry> m_data;
};

struct ScopePlotWidget::Private
{
    QwtPlot *plot;
    QwtPlotGrid *grid;
    ScopeScaleDraw *yScaleDraw;
    std::vector<QwtPlotCurve *> curves;
    ScrollZoomer *zoomer;

    constexpr static const double YSpacing = 0.5;
};

ScopePlotWidget::ScopePlotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->plot = new QwtPlot;
    d->plot->setCanvasBackground(QBrush(Qt::black));

    d->yScaleDraw = new ScopeScaleDraw;
    d->plot->setAxisScaleDraw(QwtPlot::yLeft, d->yScaleDraw);

    d->grid = new QwtPlotGrid;
    d->grid->enableX(false);
    d->grid->setPen(Qt::darkGreen, 0.0, Qt::DotLine);
    d->grid->attach(d->plot);

    d->zoomer = new ScrollZoomer(d->plot->canvas());
    d->zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    d->zoomer->setTrackerMode(QwtPicker::AlwaysOff);

    auto layout = make_vbox<0, 0>(this);
    layout->addWidget(d->plot);
}

ScopePlotWidget::~ScopePlotWidget()
{
}

std::unique_ptr<QwtPlotCurve> make_scope_curve(QwtSeriesData<QPointF> *scopeData, const QString &curveName)
{
    auto curve = std::make_unique<QwtPlotCurve>(curveName);
    curve->setData(scopeData);
    curve->setStyle(QwtPlotCurve::Steps);
    curve->setCurveAttribute(QwtPlotCurve::Inverted);
    curve->setPen(Qt::green);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);

    //curve->setItemInterest(QwtPlotItem::ScaleInterest); // TODO: try and
    //see if this information can be used for the last sample in ScopeData
    //    (to draw to the end of the x-axis).
    return curve;
}

void ScopePlotWidget::setSnapshot(
    const Snapshot &snapshot,
    const QStringList &names)
{
    // FIXME: supercrappy, resize instead of deleting existing stuff. just
    // update the data info for existing entries and replot.
    for (auto curve: d->curves)
    {
        curve->detach();
        delete curve;
    }

    d->curves.clear();
    d->yScaleDraw->clear();

    QList<double> yTicks; // major ticks for the y scale
    double yOffset = 0.0;
    int idx = 0;

    for (const auto &timeline: snapshot)
    {
        auto scopeData = new ScopeData{ timeline, yOffset };

        auto name = idx < names.size() ? names[idx] : QString::number(idx);
        auto curve = make_scope_curve(scopeData, name);
        curve->attach(d->plot);
        d->curves.push_back(curve.release());

        yTicks.push_back(yOffset);
        d->yScaleDraw->addScaleEntry(yOffset, name);

        yOffset += 1.0 + Private::YSpacing;
        ++idx;
    }

    QwtScaleDiv yScaleDiv(0.0, yOffset - Private::YSpacing);
    yScaleDiv.setTicks(QwtScaleDiv::MajorTick, yTicks);
    d->plot->setAxisScaleDiv(QwtPlot::yLeft, yScaleDiv);

    //d->plot->replot();
    d->zoomer->setZoomBase(true); // XXX:
}

#if 0
void ScopePlotWidget::addTimeline(const Timeline &timeline, const QString &name_)
{
    double yOffset = d->curves.size() * (1.0 + Private::YSpacing);
    auto name = name_.isEmpty() ? QString::number(d->curves.size()) : name_;
    d->curvesData.push_back(new ScopeData{ timeline, yOffset });
    auto curve = make_scope_curve(d->curvesData.back(), name);
    curve->attach(d->plot);
    d->curves.push_back(curve.release());
    d->plot->updateAxes();
    d->plot->replot();
}
#endif

QwtPlot *ScopePlotWidget::getPlot()
{
    return d->plot;
}

struct ScopeWidget::Private
{
    ScopeWidget *q;

    mvlc::MVLC mvlc;

    QSpinBox *spin_preTriggerTime,
             *spin_postTriggerTime;

    std::vector<QCheckBox *> checks_triggerChannels;

    QPushButton *pb_start,
                *pb_stop;

    std::thread readerThread;
    std::atomic<bool> readerQuit;
    mvlc::ThreadSafeQueue<std::vector<u32>> readerQueue;
    std::future<std::error_code> readerFuture;
    QTimer refreshTimer;
    ScopeSetup scopeSetup;

    const double YSpacing = 0.5;
    ScopePlotWidget *plot;
    std::atomic<bool> stopSampling;


    void analyze(const std::vector<u32> &buffers);

    void start()
    {
        scopeSetup = {};
        scopeSetup.preTriggerTime = spin_preTriggerTime->value();
        scopeSetup.postTriggerTime = spin_postTriggerTime->value();

        for (auto bitIdx=0u; bitIdx<checks_triggerChannels.size(); ++bitIdx)
            scopeSetup.triggerChannels.set(bitIdx, checks_triggerChannels[bitIdx]->isChecked());

        std::vector<u32> sampleBuffer;

        QProgressDialog progressDialog;
        progressDialog.setLabelText(QSL("Waiting for sample..."));
        progressDialog.setMinimum(0);
        progressDialog.setMaximum(0);

        QFutureWatcher<std::error_code> watcher;

        QObject::connect(
            &watcher, &QFutureWatcher<std::error_code>::finished,
            &progressDialog, &QDialog::close);

        QObject::connect(
            &progressDialog, &QProgressDialog::canceled,
            q, [this] () { stopSampling = true; });

        stopSampling = false;

        auto futureResult = QtConcurrent::run(
            acquire_scope_sample, mvlc, scopeSetup,
            std::ref(sampleBuffer),
            std::ref(stopSampling));

        watcher.setFuture(futureResult);
        progressDialog.exec();

        /*
        if (auto ec = futureResult.result())
        {
            qDebug() << __PRETTY_FUNCTION__ << "result=" << ec.message().c_str();
        }
        */

        analyze(sampleBuffer);
    }

    void stop()
    {
        stopSampling = true;
    }
};

void ScopeWidget::Private::analyze(const std::vector<u32> &buffer)
{
    mesytec::mvlc::util::log_buffer(std::cout, buffer, "scope buffer");

    auto snapshot = fill_snapshot_from_mvlc_buffer(buffer);
    plot->setSnapshot(snapshot);
}

ScopeWidget::ScopeWidget(mvlc::MVLC &mvlc, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->mvlc = mvlc;

    d->spin_preTriggerTime = new QSpinBox;
    d->spin_postTriggerTime = new QSpinBox;

    for (auto spin: { d->spin_preTriggerTime, d->spin_postTriggerTime })
    {
        spin->setMinimum(0);
        spin->setMaximum(std::numeric_limits<u16>::max());
        spin->setSuffix(" ns");
    }

    d->spin_preTriggerTime->setValue(200);
    d->spin_postTriggerTime->setValue(500);

#if 1
    auto channelsLayout = new QGridLayout;

    for (auto bit = 0u; bit < trigger_io::NIM_IO_Count; ++bit)
    {
        d->checks_triggerChannels.emplace_back(new QCheckBox);
        int col = trigger_io::NIM_IO_Count - bit - 1;
        channelsLayout->addWidget(new QLabel(QString::number(bit)), 0, col);
        channelsLayout->addWidget(d->checks_triggerChannels[bit], 1, col);
    }

    d->checks_triggerChannels[0]->setChecked(true); // initially let only channel 0 trigger
#endif

    auto pb_triggersAll = new QPushButton("all");
    auto pb_triggersNone = new QPushButton("none");

    connect(pb_triggersAll, &QPushButton::clicked,
            this, [this] () { for (auto cb: d->checks_triggerChannels) cb->setChecked(true); });

    connect(pb_triggersNone, &QPushButton::clicked,
            this, [this] () { for (auto cb: d->checks_triggerChannels) cb->setChecked(false); });

    channelsLayout->addWidget(pb_triggersAll, 0, trigger_io::NIM_IO_Count);
    channelsLayout->addWidget(pb_triggersNone, 1, trigger_io::NIM_IO_Count);

    d->pb_start = new QPushButton("Start");
    d->pb_stop = new QPushButton("Stop");

    connect(d->pb_start, &QPushButton::clicked, this, [this] () { d->start(); });
    connect(d->pb_stop, &QPushButton::clicked, this, [this] () { d->stop(); });

    auto buttonLayout = make_vbox();
    buttonLayout->addWidget(d->pb_start);
    buttonLayout->addWidget(d->pb_stop);

    auto controlsLayout = new QFormLayout;
    controlsLayout->addRow("Pre Trigger Time", d->spin_preTriggerTime);
    controlsLayout->addRow("Post Trigger Time", d->spin_postTriggerTime);
    controlsLayout->addRow("Trigger Channels", channelsLayout);
    //controlsLayout->addRow("Trigger Channels", combo_channels);
    controlsLayout->addRow(buttonLayout);
    auto gbControls = new QGroupBox("Setup");
    gbControls->setLayout(controlsLayout);

    d->plot = new ScopePlotWidget;

    auto widgetLayout = new QGridLayout;
    widgetLayout->addWidget(gbControls, 0, 0);
    widgetLayout->addWidget(d->plot, 0, 1);

    setLayout(widgetLayout);
    setWindowTitle("Trigger IO Osci");

    //connect(&d->refreshTimer, &QTimer::timeout,
    //        this, [this] () { d->refresh(); });
}

ScopeWidget::~ScopeWidget()
{
    d->readerQuit = true;

    if (d->readerThread.joinable())
        d->readerThread.join();
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
