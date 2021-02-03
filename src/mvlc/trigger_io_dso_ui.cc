#include "mvlc/trigger_io_dso_ui.h"

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
#include <QTableWidget>
#include <iterator>
#include <QHeaderView>
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
namespace trigger_io_dso
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
        const Trace &timeline,
        const double preTriggerTime,
        const double yOffset
        )
        : timeline(timeline)
        , preTriggerTime(preTriggerTime)
        , yOffset(yOffset)
    {
        //qDebug() << __PRETTY_FUNCTION__  << this;
    }

    ~ScopeData() override
    {
        //qDebug() << __PRETTY_FUNCTION__  << this;
    }

    QRectF boundingRect() const override
    {
        if (!timeline.empty())
        {
            double tMin = timeline.front().time.count() - preTriggerTime;
            double tMax = timeline.back().time.count() - preTriggerTime;
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
            double time = timeline[i].time.count() - preTriggerTime;
            //qDebug() << __PRETTY_FUNCTION__ << time;
            double value = static_cast<double>(timeline[i].edge);
            return { time, value + yOffset };
        }

        return {};
    }

    trigger_io_dso::Trace timeline;
    double preTriggerTime;
    double yOffset;

};

// Draws names instead of numeric coordinate values on the y axis.
class ScopeYScaleDraw: public QwtScaleDraw
{
    public:
        ~ScopeYScaleDraw() override
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
                return { it->second /* + " (y=" + QwtScaleDraw::label(value).text() + ")" */  };

            return QwtScaleDraw::label(value);
        }

        void addScaleEntry(double yOffset, const QString &label)
        {
            m_data.push_back(std::make_pair(QwtInterval(yOffset, yOffset + 1.0), label));
            invalidateCache();
        }

        void clear()
        {
            m_data.clear();
            invalidateCache();
        }

    private:
        using Entry = std::pair<QwtInterval, QString>;

        std::vector<Entry> m_data;
};

struct DSOPlotWidget::Private
{
    QwtPlot *plot;
    QwtPlotGrid *grid;
    QwtScaleDiv yScaleDiv; // copy of the y-axis scale division calculated in setTraces()
    SampleTime xMax;
    ScrollZoomer *zoomer;
    //std::unique_ptr<QwtPlotMarker> xMarker;

    std::vector<QwtPlotCurve *> curves;

    constexpr static const double YSpacing = 0.5;

    void zoomerZoomed();
};

DSOPlotWidget::DSOPlotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->plot = new QwtPlot;
    d->plot->setCanvasBackground(QBrush(Qt::white));

    d->grid = new QwtPlotGrid;
    d->grid->enableX(false);
    d->grid->setPen(Qt::darkGreen, 0.0, Qt::DotLine);
    d->grid->attach(d->plot);

    d->zoomer = new ScrollZoomer(d->plot->canvas());
    d->zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    d->zoomer->setTrackerMode(QwtPicker::AlwaysOff);

    connect(d->zoomer, &QwtPlotZoomer::zoomed,
            this, [this] (const QRectF &) { d->zoomerZoomed(); });

    //d->xMarker = std::make_unique<QwtPlotMarker>();
    //d->xMarker->setLabelAlignment( Qt::AlignLeft | Qt::AlignTop );
    //d->xMarker->setLabelOrientation( Qt::Vertical );
    //d->xMarker->setLineStyle( QwtPlotMarker::VLine );
    //d->xMarker->setLinePen(QColor("lawngreen"), 0, Qt::DashDotLine );
    //d->xMarker->attach(d->plot);

    auto layout = make_vbox<0, 0>(this);
    layout->addWidget(d->plot);
}

DSOPlotWidget::~DSOPlotWidget()
{
    //d->xMarker->detach();
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

void DSOPlotWidget::setTraces(
    const Snapshot &snapshot,
    unsigned preTriggerTime,
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
    // Always create a new scale draw instance here otherwise the y axis does
    // not properly update (it does update when zooming, so it should be
    // possible to keep the same instance).
    auto yScaleDraw = std::make_unique<ScopeYScaleDraw>();

    QList<double> yTicks; // major ticks for the y scale
    double yOffset = 0.0;
    d->xMax = SampleTime(0);
    int idx = 0;

    for (const auto &timeline: snapshot)
    {
        auto scopeData = new ScopeData(timeline, preTriggerTime, yOffset);

        auto name = idx < names.size() ? names[idx] : QString::number(idx);
        auto curve = make_scope_curve(scopeData, name);
        curve->attach(d->plot);
        d->curves.push_back(curve.release());

        yTicks.push_back(yOffset);
        yScaleDraw->addScaleEntry(yOffset, name);

        if (!timeline.empty())
            d->xMax = std::max(d->xMax, timeline.back().time);

        yOffset += 1.0 + Private::YSpacing;
        ++idx;
    }

    d->plot->setAxisScaleDraw(QwtPlot::yLeft, yScaleDraw.release());
    d->yScaleDiv.setInterval(0.0, yOffset - Private::YSpacing);
    d->yScaleDiv.setTicks(QwtScaleDiv::MajorTick, yTicks);
    d->plot->setAxisScaleDiv(QwtPlot::yLeft, d->yScaleDiv);

    //d->plot->setAxisScale(QwtPlot::xBottom, 0.0, d->xMax.count());

    // Tells the zoomer that we're currently completely zoomed out. Does a
    // plot->replot() unless the arg is false.
    d->zoomer->setZoomBase(true);
    d->plot->replot();
}

void DSOPlotWidget::Private::zoomerZoomed()
{
    qDebug() << __PRETTY_FUNCTION__ << this << zoomer->zoomRectIndex();

    if (zoomer->zoomRectIndex() == 0)
        this->plot->setAxisAutoScale(QwtPlot::xBottom);

    plot->setAxisScaleDiv(QwtPlot::yLeft, yScaleDiv);
    plot->replot();
}

QwtPlot *DSOPlotWidget::getQwtPlot()
{
    return d->plot;
}

//
// DSOControlWidget
//

namespace
{
template<typename Func>
void for_all_table_items(QTableWidget *table, Func f)
{
    for (int row=0; row<table->rowCount(); ++row)
        for (int col=0; col<table->columnCount(); ++col)
            if (auto item = table->item(row, col))
                f(item);
}
}

struct DSOControlWidget::Private
{
    QSpinBox *spin_preTriggerTime,
             *spin_postTriggerTime,
             *spin_interval;

    static const int TriggerCols = 5;
    static const int TriggerRows = 4;

    QTableWidget *table_triggers;
    QWidget *setupWidget;
    QPushButton *pb_start,
                *pb_stop;
};

DSOControlWidget::DSOControlWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    setWindowTitle("DSOControlWidget");
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

    d->table_triggers = new QTableWidget(Private::TriggerRows, Private::TriggerCols);

    for (int row=0; row < Private::TriggerRows; ++row)
    {
        for (int col=0; col < Private::TriggerCols; ++col)
        {
            size_t trigNum = row * Private::TriggerCols + col;

            auto item = std::make_unique<QTableWidgetItem>();
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);

            if (trigNum < NIM_IO_Count)
                item->setText(QSL("NIM%1").arg(trigNum));
            else
                item->setText(QSL("IRQ%1").arg(trigNum - NIM_IO_Count + 1));


            d->table_triggers->setItem(row, col, item.release());
        }
    }

    d->table_triggers->verticalHeader()->hide();
    d->table_triggers->horizontalHeader()->hide();
    d->table_triggers->resizeColumnsToContents();
    d->table_triggers->resizeRowsToContents();
    d->table_triggers->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->table_triggers->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->table_triggers->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    d->table_triggers->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    auto pb_triggersAll = new QPushButton("all");
    auto pb_triggersNone = new QPushButton("none");

    d->spin_interval = new QSpinBox;
    d->spin_interval->setMinimum(0);
    d->spin_interval->setMaximum(5000);
    d->spin_interval->setSingleStep(500);
    d->spin_interval->setSpecialValueText("once");
    d->spin_interval->setSuffix(" ms");
    d->spin_interval->setValue(1000);

    auto gb_triggers = new QGroupBox("Trigger Channels");
    gb_triggers->setAlignment(Qt::AlignCenter);
    auto l_triggers = make_grid(gb_triggers);
    l_triggers->addWidget(d->table_triggers, 0, 0, 1, 2);
    l_triggers->addWidget(pb_triggersAll, 1, 0);
    l_triggers->addWidget(pb_triggersNone, 1, 1);

    d->setupWidget = new QWidget;
    auto setupLayout = new QFormLayout(d->setupWidget);
    setupLayout->addRow("Pre Trigger Time", d->spin_preTriggerTime);
    setupLayout->addRow("Post Trigger Time",d->spin_postTriggerTime);
    setupLayout->addRow(gb_triggers);
    setupLayout->addRow("Interval", d->spin_interval);

    d->pb_start = new QPushButton("Start DSO");
    d->pb_stop = new QPushButton("Stop DSO");
    d->pb_stop->setEnabled(false);

    auto controlLayout = make_hbox();
    controlLayout->addWidget(d->pb_start);
    controlLayout->addWidget(d->pb_stop);

    auto widgetLayout = make_vbox<4, 4>();
    widgetLayout->addWidget(d->setupWidget);
    widgetLayout->addLayout(controlLayout);

    setLayout(widgetLayout);

    connect(pb_triggersAll, &QPushButton::clicked, this, [this] () {
                for_all_table_items(d->table_triggers, [] (auto item) {
                    item->setCheckState(Qt::Checked);
                });
            });

    connect(pb_triggersNone, &QPushButton::clicked, this, [this] () {
                for_all_table_items(d->table_triggers, [] (auto item) {
                    item->setCheckState(Qt::Unchecked);
                });
            });

    connect(d->pb_start, &QPushButton::clicked, this, [this] () {
        emit startDSO(getDSOSetup(), getInterval());
    });

    connect(d->pb_stop, &QPushButton::clicked, this, [this] () {
        emit stopDSO();
    });
}

DSOControlWidget::~DSOControlWidget()
{
}

void DSOControlWidget::setDSOActive(bool active)
{
    d->setupWidget->setEnabled(!active);
    d->pb_start->setEnabled(!active);
    d->pb_stop->setEnabled(active);
}

DSOSetup DSOControlWidget::getDSOSetup() const
{
    DSOSetup setup = {};
    setup.preTriggerTime = d->spin_preTriggerTime->value();
    setup.postTriggerTime = d->spin_postTriggerTime->value();

    for_all_table_items(d->table_triggers, [&setup] (auto item) {
        if (item->checkState() == Qt::Checked)
        {
            size_t trigNum = item->row() * Private::TriggerCols + item->column();

            if (trigNum < NIM_IO_Count)
                setup.nimTriggers.set(trigNum);
            else
                setup.irqTriggers.set(trigNum - NIM_IO_Count);
        }
    });

    return setup;
}

std::chrono::milliseconds DSOControlWidget::getInterval() const
{
    return std::chrono::milliseconds(d->spin_interval->value());
}

void DSOControlWidget::setDSOSetup(
    const DSOSetup &setup,
    const std::chrono::milliseconds &interval)
{
    d->spin_preTriggerTime->setValue(setup.preTriggerTime);
    d->spin_postTriggerTime->setValue(setup.postTriggerTime);

    for_all_table_items(d->table_triggers, [&setup] (auto item) {
            size_t trigNum = item->row() * Private::TriggerCols + item->column();
            bool checked = false;

            if (trigNum < NIM_IO_Count)
                checked = setup.nimTriggers.test(trigNum);
            else
                checked = setup.irqTriggers.test(trigNum - NIM_IO_Count);

            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    });

    d->spin_interval->setValue(interval.count());
}

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
