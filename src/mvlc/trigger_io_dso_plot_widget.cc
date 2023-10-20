#include "mvlc/trigger_io_dso_plot_widget.h"

#include <chrono>
#include <cmath>
#include <iterator>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <qnamespace.h>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QtConcurrent>
#include <QTimer>
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
namespace trigger_io
{

namespace
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


        assert(i < trace.size()); // TODO: figure out/decide if it's ok to hit this case
        return { 0.0, yOffset };
    }

    Edge sampleEdge(size_t i) const
    {
        if (i < trace.size())
            return trace[i].edge;
        return Edge::Unknown;
    }

    QwtInterval interval() const
    {
        return QwtInterval(yOffset, yOffset + 1.0);
    }

    Trace trace;
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
            assert(from <= to);

            auto sd = scopeData();
            int unknownSamples = 0;

            #if 0
            // This version draws a red line at the end of traces if the sample value is "Unknown".
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
            #else

            // This version draws a red line at the front of traces if the sample value is "Unknown".
            for (int i=from; i<=to; ++i)
            {
                if (sd->sampleEdge(i) == Edge::Unknown)
                    ++unknownSamples;
            }

            if (unknownSamples > 0)
            {
                painter->save();
                painter->setPen(Qt::darkRed);
                //QwtPlotCurve::drawSteps(painter, xMap, yMap, canvasRect, from, from+unknownSamples-1); // does not color the transition from unknown to known
                QwtPlotCurve::drawSteps(painter, xMap, yMap, canvasRect, from, from+unknownSamples); // does color the transition
                painter->restore();
                //QwtPlotCurve::drawSteps(painter, xMap, yMap, canvasRect, from+unknownSamples-1, to);
                QwtPlotCurve::drawSteps(painter, xMap, yMap, canvasRect, from+unknownSamples, to);
            }
            else
            {
                QwtPlotCurve::drawSteps(painter, xMap, yMap, canvasRect, from, to);
            }


            #endif
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

class DSOPlotMouseTracker: public QwtPlotPicker
{
    public:
        using QwtPlotPicker::QwtPlotPicker;

    protected:
        QwtText trackerTextF(const QPointF &pos) const override
        {
            if (rubberBand() != QwtPicker::VLineRubberBand)
                return QwtPlotPicker::trackerTextF(pos);

            return QwtText(QSL("t=%1").arg(QString::number(std::floor(pos.x()) + 0.5)));
        }
};

} // end anon namespace

struct DSOPlotWidget::Private
{
    constexpr static const double YSpacing = 0.5;

    DSOPlotWidget *q;
    QwtPlot *plot;
    QwtPlotGrid *grid;
    QwtScaleDiv yScaleDiv; // copy of the y-axis scale division calculated in setTraces()
    QwtInterval xAxisInterval;
    ScrollZoomer *zoomer;
    QwtPlotPicker *mousePosTracker;
    double lastMousePosPickerX = 0.0;
    std::unique_ptr<QwtPlotMarker> triggerTimeMarker;
    std::unique_ptr<QwtPlotMarker> preTriggerTimeMarker;
    std::unique_ptr<QwtPlotMarker> postTriggerTimeMarker;

    std::vector<ScopeCurve *> curves;
    std::vector<QwtPlotMarker *> curveValueLabels;

    void replot();

    void updateCurveValueLabels()
    {
        if (!mousePosTracker->isEnabled())
            return;

        for (size_t curveIdx = 0; curveIdx < curveValueLabels.size(); ++curveIdx)
        {
            auto curve = curves[curveIdx];
            auto scopeData = reinterpret_cast<const ScopeData *>(curve->data());
            SampleTime st(lastMousePosPickerX + scopeData->preTriggerTime);
            Edge edge = edge_at(scopeData->trace, st);

            auto marker = curveValueLabels[curveIdx];
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
    d->plot->axisWidget(QwtPlot::xBottom)->setTitle("Time [ns]");

    d->grid = new QwtPlotGrid;
    d->grid->enableX(false);
    d->grid->setPen(Qt::darkGreen, 0.0, Qt::DotLine);
    d->grid->attach(d->plot);

    d->zoomer = new ScrollZoomer(d->plot->canvas()); // Note: canvas is also the zoomers parent
    d->zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    d->zoomer->setHScrollBarMode(Qt::ScrollBarAlwaysOn);
    d->zoomer->setTrackerMode(QwtPicker::AlwaysOff);

    // Draws a vertical line and the x-coordinate at the current mouse position
    // inside the plot.
    d->mousePosTracker = new DSOPlotMouseTracker(d->plot->canvas());
    d->mousePosTracker->setTrackerMode(QwtPicker::AlwaysOn);
    d->mousePosTracker->setRubberBand(QwtPicker::VLineRubberBand);
    {
        QPen pen(Qt::black, 1.0, Qt::DotLine);
        d->mousePosTracker->setRubberBandPen(pen);
    }
    d->mousePosTracker->setStateMachine(new QwtPickerTrackerMachine);

    // Record picker pos and update the trace value labels inside the plot.
    connect(d->mousePosTracker, &QwtPlotPicker::moved,
            this, [this] (const QPointF &pos) {
                d->lastMousePosPickerX = pos.x();
                d->updateCurveValueLabels();
            });

    // The picker emits this on mouse enter/leave. Show/hide the trace value
    // labels accordingly.
    connect(d->mousePosTracker, &QwtPlotPicker::activated,
            this, [this] (bool active)
            {
                for (auto marker: d->curveValueLabels)
                    marker->setVisible(active);
                d->replot();
            });

    // When starting to zoom (dragging the zoom rectangle with the mouse)
    // disable the curve value labels and the picker for the mouse position.
    connect(d->zoomer, &QwtPicker::activated,
            this, [this] (bool zoomerActive)
            {
                d->mousePosTracker->setEnabled(!zoomerActive);
                for (auto marker: d->curveValueLabels)
                    marker->setVisible(!zoomerActive && d->mousePosTracker->isActive());
                d->replot();
            });

    connect(d->zoomer, &QwtPlotZoomer::zoomed,
            this, [this] (const QRectF &) { d->replot(); });

    auto add_time_marker = [this] (const QString &label, bool alignLeft = true)
    {
        auto marker = std::make_unique<QwtPlotMarker>();
        marker->setLabelAlignment((alignLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignTop);
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
    d->preTriggerTimeMarker = add_time_marker("\nPre Trigger", false);
    d->preTriggerTimeMarker->hide();
    d->postTriggerTimeMarker = add_time_marker("\nPost Trigger");
    d->postTriggerTimeMarker->hide();

    // Reacts to clicks on the scale. Does not work for the labels but only for
    // the area enclosing the scale ticks.
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

    // Delete curve value labels
    for (auto marker: d->curveValueLabels)
    {
        marker->detach();
        delete marker;
    }
    d->curveValueLabels.clear();

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
        marker->setVisible(d->mousePosTracker->isActive());
        d->curveValueLabels.push_back(marker);

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

    d->updateCurveValueLabels();
}

void DSOPlotWidget::setPreTriggerTime(double preTrigger)
{
    d->preTriggerTimeMarker->setXValue(preTrigger);
    d->preTriggerTimeMarker->show();
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

} // end namespace trigger_io
} // end namespace mvme_mvlc
} // end namespace mesytec
