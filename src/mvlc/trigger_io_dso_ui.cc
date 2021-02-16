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
#include <qwt_picker_machine.h>
#include <qwt_symbol.h>

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
        const Trace &trace,
        const double preTriggerTime,
        const double yOffset
        )
        : trace(trace)
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
        if (!trace.empty())
        {
            double tMin = trace.front().time.count() - preTriggerTime;
            double tMax = trace.back().time.count() - preTriggerTime;
            double tRange = tMax - tMin;
            auto result = QRectF(tMin, yOffset, tRange, 1.0);
            //qDebug() << __PRETTY_FUNCTION__ << "result=" << result;
            return result;
        }

        return {};
    }

    size_t size() const override
    {
        return trace.size();
    }

    QPointF sample(size_t i) const override
    {
        if (i < trace.size())
        {
            double time = trace[i].time.count() - preTriggerTime;
            //qDebug() << __PRETTY_FUNCTION__ << time;
            double value = static_cast<double>(trace[i].edge);
            if (trace[i].edge == Edge::Unknown)
                value = 0.5;
            return { time, value + yOffset };
        }

        return {};
    }

    Edge sampleEdge(size_t i) const
    {
        if (i < trace.size())
            return trace[i].edge;
        return Edge::Unknown;
    }

    QwtInterval interval() const
    {
        return QwtInterval(yOffset, 1.0);
    }

    trigger_io_dso::Trace trace;
    double preTriggerTime;
    double yOffset;

};

class ScopeCurve: public QwtPlotCurve
{
    public:
        using QwtPlotCurve::QwtPlotCurve;

        const ScopeData *scopeData() const
        {
            return reinterpret_cast<const ScopeData *>(data());
        }

    protected:
        void drawSteps(
            QPainter *painter,
            const QwtScaleMap &xMap,
            const QwtScaleMap &yMap,
            const QRectF &canvasRect,
            int from,
            int to
            ) const override
        {
            auto sd = scopeData();

            int unknownSamples = 0;

            for (int i=to; i>=from; --i)
            {
                if (sd->sampleEdge(i) == Edge::Unknown)
                    ++unknownSamples;
            }

            //qDebug() << __PRETTY_FUNCTION__ << "from=" << from << ", to=" << to
            //    << "unknownSamples=" << unknownSamples
            //    << ", #samples=" << sd->size();

            QwtPlotCurve::drawSteps(painter, xMap, yMap, canvasRect, from, to-unknownSamples);

            painter->setPen(Qt::darkRed);

            QwtPlotCurve::drawSteps(painter, xMap, yMap, canvasRect, (to-unknownSamples)+1, to);
        }
};

// Draws names instead of numeric coordinate values on the y axis.
class ScopeYScaleDraw: public QwtScaleDraw
{
    public:
        ~ScopeYScaleDraw() override
        {
            //qDebug() << __PRETTY_FUNCTION__ << this;
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

namespace
{
QString edge_to_marker_text(Edge edge)
{
    switch (edge)
    {
        case Edge::Falling:
            return "0";
        case Edge::Rising:
            return "1";
        case Edge::Unknown:
            return "unk";
        }
    return {};
}
}

struct DSOPlotWidget::Private
{
    constexpr static const double YSpacing = 0.5;

    DSOPlotWidget *q;
    QwtPlot *plot;
    QwtPlotGrid *grid;
    QwtScaleDiv yScaleDiv; // copy of the y-axis scale division calculated in setTraces()
    QwtInterval xAxisInterval;
    ScrollZoomer *zoomer;
    QwtPlotPicker *mousePosPicker;
    double lastMousePosPickerX = 0.0;
    std::unique_ptr<QwtPlotMarker> triggerTimeMarker;
    std::unique_ptr<QwtPlotMarker> postTriggerTimeMarker;

    std::vector<ScopeCurve *> curves;
    std::vector<QwtPlotMarker *> curveMarkers;

    void replot();

    void updateCurveMarkers()
    {
        if (!mousePosPicker->isEnabled())
            return;

        for (size_t curveIdx = 0; curveIdx < curveMarkers.size(); ++curveIdx)
        {
            auto curve = curves[curveIdx];
            auto scopeData = reinterpret_cast<const ScopeData *>(curve->data());
            SampleTime st(lastMousePosPickerX + scopeData->preTriggerTime);
            Edge edge = edge_at(scopeData->trace, st);

            auto marker = curveMarkers[curveIdx];
            marker->setXValue(lastMousePosPickerX);
            marker->setLabel(QwtText(edge_to_marker_text(edge)));
        }

        this->replot();
    }

    void onYScaleClicked(double yValue)
    {
        for (size_t curveIdx = 0; curveIdx < curves.size(); ++curveIdx)
        {
            auto curve = curves[curveIdx];
            auto scopeData = curve->scopeData();

            if (scopeData->interval().contains(yValue))
            {
                auto name = curve->title().text();
                const auto &trace = scopeData->trace;
                emit q->traceClicked(trace, name);
            }
        }
    }
};

DSOPlotWidget::DSOPlotWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->plot = new QwtPlot;
    d->plot->setCanvasBackground(QBrush(Qt::white));

    d->grid = new QwtPlotGrid;
    d->grid->enableX(false);
    d->grid->setPen(Qt::darkGreen, 0.0, Qt::DotLine);
    d->grid->attach(d->plot);

    d->zoomer = new ScrollZoomer(d->plot->canvas()); // Note: canvas is also the zoomers parent
    d->zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    d->zoomer->setHScrollBarMode(Qt::ScrollBarAlwaysOn);
    d->zoomer->setTrackerMode(QwtPicker::AlwaysOff);

    // Draws a vertical line at the current cursor position and keep track of
    // the cursor x-coordinate.
    d->mousePosPicker = new QwtPlotPicker(d->plot->canvas());
    d->mousePosPicker->setTrackerMode(QwtPicker::AlwaysOn);
    d->mousePosPicker->setRubberBand(QwtPicker::VLineRubberBand);
    {
        QPen pen(Qt::black, 1.0, Qt::DotLine);
        d->mousePosPicker->setRubberBandPen(pen);
    }
    d->mousePosPicker->setStateMachine(new QwtPickerTrackerMachine);

    connect(d->mousePosPicker, &QwtPlotPicker::moved,
            this, [this] (const QPointF &pos) {
                d->lastMousePosPickerX = pos.x();
                d->updateCurveMarkers();
            });

    connect(d->zoomer, &QwtPicker::activated,
            this, [this] (bool zoomerActive)
            {
                d->mousePosPicker->setEnabled(!zoomerActive);
                for (auto marker: d->curveMarkers)
                    marker->setVisible(!zoomerActive);
                d->replot();
            });

    connect(d->zoomer, &QwtPlotZoomer::zoomed,
            this, [this] (const QRectF &) { d->replot(); });

    auto add_time_marker = [this] (const QString &label)
    {
        auto marker = std::make_unique<QwtPlotMarker>();
        marker->setLabelAlignment( Qt::AlignLeft | Qt::AlignTop );
        marker->setLabelOrientation( Qt::Horizontal );
        marker->setLineStyle( QwtPlotMarker::VLine );
        marker->setLinePen(QColor("black"), 0, Qt::DashDotLine );
        marker->setLabel(QwtText(label));
        marker->attach(d->plot);
        return marker;
    };

    // Start with a newline to hopefully render the label below the zoomers
    // scrollbar.
    d->triggerTimeMarker = add_time_marker("\nTrigger");
    d->postTriggerTimeMarker = add_time_marker("\nPost Trigger");
    d->postTriggerTimeMarker->hide();

    auto scalePicker = new ScalePicker(d->plot);
    connect(scalePicker, &ScalePicker::clicked,
            this, [this] (int axis, double value)
            {
                //qDebug() << __PRETTY_FUNCTION__ << axis << value;
                if (axis == QwtPlot::yLeft)
                    d->onYScaleClicked(value);
            });

    auto layout = make_vbox<0, 0>(this);
    layout->addWidget(d->plot);
}

DSOPlotWidget::~DSOPlotWidget()
{
    d->triggerTimeMarker->detach();
}

std::unique_ptr<ScopeCurve> make_scope_curve(QwtSeriesData<QPointF> *scopeData, const QString &curveName)
{
    auto curve = std::make_unique<ScopeCurve>(curveName);
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
    // Deletes all existing curves. It would be better to just update their data
    // entries but this works for now.
    for (auto curve: d->curves)
    {
        curve->detach();
        delete curve;
    }

    d->curves.clear();

    for (auto marker: d->curveMarkers)
    {
        marker->detach();
        delete marker;
    }

    d->curveMarkers.clear();

    // Always create a new scale draw instance here otherwise the y axis does
    // not properly update (it does update when zooming, so it should be
    // possible to somehow keep the same instance). FIXME: fix this O.o
    auto yScaleDraw = std::make_unique<ScopeYScaleDraw>();

    QList<double> yTicks; // major ticks for the y scale
    double yOffset = 0.0;
    const double yStep = 1.0 + Private::YSpacing;
    int idx = 0;

    for (const auto &trace: snapshot)
    {
        auto scopeData = new ScopeData(trace, preTriggerTime, yOffset);

        auto name = idx < names.size() ? names[idx] : QString::number(idx);
        auto curve = make_scope_curve(scopeData, name);
        curve->attach(d->plot);
        d->curves.push_back(curve.release());

        yTicks.push_back(yOffset);
        yScaleDraw->addScaleEntry(yOffset, name);

        // Horizontal dotted 0 level line for the trace.
        auto marker = new QwtPlotMarker;
        marker->setYValue(yOffset + 0.5);
        marker->setLabelAlignment(Qt::AlignLeft | Qt::AlignCenter);
        marker->attach(d->plot);
        d->curveMarkers.push_back(marker);

        yOffset += yStep;
        ++idx;
    }

    // Scale the y axis as if we would draw at least 10 traces. This avoids
    // having a single trace scale over the whole vertical area which looks
    // ridiculous.
    double yScaleMaxValue = yOffset;

    if (yScaleMaxValue < 10 * yStep)
        yScaleMaxValue = 10 * yStep;

    d->plot->setAxisScaleDraw(QwtPlot::yLeft, yScaleDraw.release());
    d->yScaleDiv.setInterval(0.0, yScaleMaxValue);
    d->yScaleDiv.setTicks(QwtScaleDiv::MajorTick, yTicks);

    d->updateCurveMarkers();
}

void DSOPlotWidget::setPostTriggerTime(double postTrigger)
{
    d->postTriggerTimeMarker->setXValue(postTrigger);
    d->postTriggerTimeMarker->show();
}

void DSOPlotWidget::setTriggerTraceInfo(const std::vector<bool> &isTriggerTrace)
{
    const size_t maxIdx = std::min(d->curves.size(), isTriggerTrace.size());

    for (size_t i=0; i<maxIdx; ++i)
    {
        QPen pen;
        if (isTriggerTrace[i])
            pen.setColor(Qt::darkGreen);
        else
            pen.setColor(Qt::green);

        d->curves[i]->setPen(pen);
    }
}

void DSOPlotWidget::setXInterval(double xMin, double xMax)
{
    d->xAxisInterval = QwtInterval(xMin, xMax);
    d->replot();
}

void DSOPlotWidget::setXAutoScale()
{
    d->xAxisInterval.invalidate();
    d->replot();
}

void DSOPlotWidget::Private::replot()
{
    // Undoes any zooming on the y axis
    plot->setAxisScaleDiv(QwtPlot::yLeft, yScaleDiv);

    if (zoomer->zoomRectIndex() == 0)
    {
        if (xAxisInterval.isValid())
        {
            plot->setAxisScale(QwtPlot::xBottom, xAxisInterval.minValue(), xAxisInterval.maxValue());
        }
        else
        {
            plot->setAxisAutoScale(QwtPlot::xBottom);
        }

        zoomer->setZoomBase(true);
    }
    else
    {
        plot->replot();
    }
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
    d->spin_interval->setSingleStep(10);
    d->spin_interval->setSpecialValueText("once");
    d->spin_interval->setSuffix(" ms");
    d->spin_interval->setValue(500);

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
        emit startDSO();
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
