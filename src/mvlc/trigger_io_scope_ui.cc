#include "mvlc/trigger_io_scope_ui.h"

#include <chrono>
#include <QBoxLayout>
#include <QCheckBox>
#include <QDebug>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include "mesytec-mvlc/util/threadsafequeue.h"
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
// When set on a curve via setData the curve takes ownership.  Timeline
// contains the samples to plot, yOffset is used to draw multiple traces in the
// same plot aligned to different y coordinates, the preTriggerTime is used for
// correct x-axis scaling (time values (0, preTriggerTime) are mapped to
// (-preTriggerTime, 0) so that the trigger is always at 0.
struct ScopeData: public QwtSeriesData<QPointF>
{
    ScopeData(
        const trigger_io_scope::Timeline &timeline,
        const double yOffset,
        const unsigned preTriggerTime
        )
        : timeline(timeline)
        , yOffset(yOffset)
        , preTriggerTime(preTriggerTime)
    {}

    ~ScopeData()
    {
        qDebug() << __PRETTY_FUNCTION__  << this;
    }

    virtual QRectF boundingRect() const override
    {
        auto tMin = std::numeric_limits<u16>::max();
        auto tMax = std::numeric_limits<u16>::min();

        for (const auto &sample: timeline)
        {
            tMin = std::min(tMin, sample.time);
            tMax = std::max(tMax, sample.time);
        }

        // casts needed to move from unsigned to signed floating point arithmetic
        double tMinF = static_cast<double>(tMin) - preTriggerTime;
        double tRange = static_cast<double>(tMax) - tMin;

        return QRectF(tMinF, yOffset, tRange, 1.0);
        //return QRectF(tMin, yOffset, tMax-tMin, 1.0);
    }

    size_t size() const override
    {
        return timeline.size();
    }

    virtual QPointF sample(size_t i) const override
    {
        double time = timeline[i].time;
        double value = static_cast<double>(timeline[i].edge);
        return { time - preTriggerTime,  value + yOffset };
    }

    trigger_io_scope::Timeline timeline;
    double yOffset;
    unsigned preTriggerTime;

};

struct ScopePlotWidget::Private
{
    QwtPlot *plot;
    std::vector<ScopeData *> curvesData;
    std::vector<QwtPlotCurve *> curves;
    ScopeSetup setup;

    constexpr static const double YSpacing = 0.5;
};

ScopePlotWidget::ScopePlotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->plot = new QwtPlot;

    auto layout = make_vbox(this);
    layout->addWidget(d->plot);
}

ScopePlotWidget::~ScopePlotWidget()
{
}

void ScopePlotWidget::setSnapshot(const ScopeSetup &setup, const Snapshot &snapshot)
{
    for (auto curve: d->curves)
    {
        curve->detach();
        curve->setData(nullptr);
        delete curve;
    }

    d->curves.clear();
    d->curvesData.clear();

    d->setup = setup;
    double yOffset = 0.0;

    for (const auto &timeline: snapshot)
    {
        d->curvesData.push_back(new ScopeData{ timeline, yOffset, setup.preTriggerTime });
        yOffset += 1.0 + Private::YSpacing;
    }

    int idx = 0;
    for (auto &scopeData: d->curvesData)
    {
        auto curve = new QwtPlotCurve(QString::number(idx));
        curve->setData(scopeData);
        curve->setStyle(QwtPlotCurve::Steps);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased);
        curve->attach(d->plot);
        d->curves.push_back(curve);
        ++idx;
    }

    d->plot->setAxisScale(QwtPlot::yLeft, 0.0, yOffset);
    d->plot->enableAxis(QwtPlot::yLeft, false);
    d->plot->updateAxes();
    d->plot->replot();
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
    QwtPlot *plot;


    void start()
    {
        if (readerThread.joinable())
            return;

        scopeSetup = {};
        scopeSetup.preTriggerTime = spin_preTriggerTime->value();
        scopeSetup.postTriggerTime = spin_postTriggerTime->value();

        for (auto bitIdx=0u; bitIdx<checks_triggerChannels.size(); ++bitIdx)
            scopeSetup.triggerChannels.set(bitIdx, checks_triggerChannels[bitIdx]->isChecked());

        std::promise<std::error_code> promise;
        readerFuture = promise.get_future();
        readerQuit = false;

        readerThread = std::thread(
            reader, mvlc, scopeSetup, std::ref(readerQueue), std::ref(readerQuit),
            std::move(promise));

        refreshTimer.setInterval(500);
        refreshTimer.start();
        pb_start->setEnabled(false);
        pb_stop->setEnabled(true);
    }

    void stop()
    {
        refreshTimer.stop();
        readerQuit = true;

        if (readerThread.joinable())
            readerThread.join();

        qDebug() << __PRETTY_FUNCTION__ << readerFuture.get().message().c_str();

        refresh();
    }

    void analyze(const std::vector<std::vector<u32>> &buffers);

    void refresh()
    {
        std::vector<std::vector<u32>> buffers;

        while (true)
        {
            std::vector<u32> buffer = readerQueue.dequeue();

            if (buffer.empty())
                break;

            buffers.emplace_back(buffer);
        }

        qDebug() << __PRETTY_FUNCTION__ << "got" << buffers.size() << "buffers from readerQueue";

        analyze(buffers);

        auto fs = readerFuture.wait_for(std::chrono::milliseconds(1));

        if (fs == std::future_status::ready)
        {
            refreshTimer.stop();
            pb_start->setEnabled(true);
            pb_stop->setEnabled(false);
            if (readerThread.joinable())
                readerThread.join();
        }
    }
};

void ScopeWidget::Private::analyze(const std::vector<std::vector<u32>> &buffers)
{
    if (buffers.empty()) return;

    const auto &mostRecent = buffers.back();
    auto snapshot = fill_snapshot(mostRecent);
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

    auto channelsLayout = new QGridLayout;

    for (auto bit = 0u; bit < trigger_io::NIM_IO_Count; ++bit)
    {
        d->checks_triggerChannels.emplace_back(new QCheckBox);
        int col = trigger_io::NIM_IO_Count - bit - 1;
        channelsLayout->addWidget(new QLabel(QString::number(bit)), 0, col);
        channelsLayout->addWidget(d->checks_triggerChannels[bit], 1, col);
    }

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
    d->pb_stop->setEnabled(false);

    auto buttonLayout = make_vbox();
    buttonLayout->addWidget(d->pb_start);
    buttonLayout->addWidget(d->pb_stop);

    auto controlsLayout = new QFormLayout;
    controlsLayout->addRow("Pre Trigger Time", d->spin_preTriggerTime);
    controlsLayout->addRow("Post Trigger Time", d->spin_postTriggerTime);
    controlsLayout->addRow("Trigger Channels", channelsLayout);
    controlsLayout->addRow(buttonLayout);

    setLayout(controlsLayout);
    setWindowTitle("Trigger IO Osci");

    connect(&d->refreshTimer, &QTimer::timeout,
            this, [this] () { d->refresh(); });
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
