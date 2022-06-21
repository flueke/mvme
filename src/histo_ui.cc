#include "histo_ui.h"

#include <QBoxLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QToolBar>
#include <QStatusBar>
#include <QDebug>
#include <memory>
#include <qwt_scale_map.h>
#include <spdlog/spdlog.h>
#include <qwt_plot_zoneitem.h>
#include <qwt_plot_marker.h>
#include <qwt_scale_engine.h>

#include "histo_gui_util.h"
#include "histo_util.h"

namespace histo_ui
{

QRectF canvas_to_scale(const QwtPlot *plot, const QRect &rect)
{
    const QwtScaleMap xMap = plot->canvasMap( QwtPlot::xBottom);
    const QwtScaleMap yMap = plot->canvasMap( QwtPlot::yLeft);

    return QwtScaleMap::invTransform( xMap, yMap, rect );
}

QPointF canvas_to_scale(const QwtPlot *plot, const QPoint &pos)
{
    const QwtScaleMap xMap = plot->canvasMap( QwtPlot::xBottom);
    const QwtScaleMap yMap = plot->canvasMap( QwtPlot::yLeft);

    return QPointF(
        xMap.invTransform( pos.x() ),
        yMap.invTransform( pos.y() )
        );
}

IPlotWidget::~IPlotWidget()
{
}

struct PlotWidget::Private
{
    Private(PlotWidget *q_)
        : q(q_)
    {
    }

    PlotWidget *q;
    QToolBar *toolbar;
    QwtPlot *plot;
    QStatusBar *statusbar;
    QVBoxLayout *layout;
};

PlotWidget::PlotWidget(QWidget *parent)
    : IPlotWidget(parent)
    , d(std::make_unique<Private>(this))
{
    d->toolbar = new QToolBar;
    d->plot = new QwtPlot;
    d->statusbar = new QStatusBar;

    d->plot->setMouseTracking(true);
    d->plot->installEventFilter(this);

    d->plot->canvas()->setMouseTracking(true);
    d->plot->installEventFilter(this);

    auto toolbarFrame = new QFrame;
    toolbarFrame->setFrameStyle(QFrame::StyledPanel);
    {
        auto layout = new QHBoxLayout(toolbarFrame);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(d->toolbar);
    }

    d->layout = new QVBoxLayout(this);
    d->layout->setContentsMargins(2, 2, 2, 2);
    d->layout->setSpacing(2);
    d->layout->addWidget(toolbarFrame);
    d->layout->addWidget(d->plot);
    d->layout->addWidget(d->statusbar);
    d->layout->setStretch(1, 1);

    resize(600, 400);
}

PlotWidget::~PlotWidget()
{
}

QwtPlot *PlotWidget::getPlot()
{
    return d->plot;
}

const QwtPlot *PlotWidget::getPlot() const
{
    return d->plot;
}

QToolBar *PlotWidget::getToolBar()
{
    return d->toolbar;
}

QStatusBar *PlotWidget::getStatusBar()
{
    return d->statusbar;
}

void PlotWidget::replot()
{
    emit aboutToReplot();
    getPlot()->replot();
}

bool PlotWidget::eventFilter(QObject *object, QEvent *event)
{
    if (object && object == getPlot())
    {
        switch (event->type())
        {
            case QEvent::Enter:
                emit mouseEnteredPlot();
                break;

            case QEvent::Leave:
                emit mouseLeftPlot();
                break;

            case QEvent::MouseMove:
                {
                    auto me = static_cast<const QMouseEvent *>(event);
                    auto pos = getPlot()->canvas()->mapFromGlobal(me->globalPos());
                    emit mouseMoveOnPlot(canvas_to_scale(getPlot(), pos));
                }
                break;

            default:
                break;
        }
    }

    return QWidget::eventFilter(object, event);
}

namespace
{
    QwtPlotMarker *make_position_marker()
    {
        static const double PlotTextLayerZ  = 1000.0;

        auto marker = new QwtPlotMarker;
        marker->setLabelAlignment( Qt::AlignLeft | Qt::AlignTop );
        marker->setLabelOrientation( Qt::Vertical );
        marker->setLineStyle( QwtPlotMarker::VLine );
        marker->setLinePen( Qt::black, 0, Qt::DashDotLine );
        marker->setZ(PlotTextLayerZ);

        return marker;
    }

    struct IntervalPlotItems
    {
        std::array<QwtPlotMarker *, 2> markers;
        QwtPlotZoneItem *zone;
    };

    static const int CanStartDragDistancePixels = 4;
}

PlotPicker::PlotPicker(QWidget *canvas)
    : QwtPlotPicker(canvas)
{
    connect(this, qOverload<const QPoint &>(&QwtPicker::removed),
            this, [this] (const QPoint &p)
            {
                emit removed(invTransform(p));
            });
}

PlotPicker::PlotPicker(int xAxis, int yAxis,
                       RubberBand rubberBand,
                       DisplayMode trackerMode,
                       QWidget *canvas)
    : QwtPlotPicker(xAxis, yAxis, rubberBand, trackerMode, canvas)
{
    connect(this, qOverload<const QPoint &>(&QwtPicker::removed),
            this, [this] (const QPoint &p)
            {
                emit removed(invTransform(p));
            });
}


struct NewIntervalPicker::Private
{
    NewIntervalPicker *q;
    QwtPlot *plot;
    std::array<QwtPlotMarker *, 2> markers;
    QwtPlotZoneItem * zoneItem;
    QVector<QPointF> selectedPoints;

    QwtInterval getInterval() const
    {
        if (selectedPoints.size() != 2)
            return {};

        return QwtInterval(selectedPoints[0].x(), selectedPoints[1].x()).normalized();
    }

    void updateMarkersAndZone()
    {
        zoneItem->hide();
        markers[0]->hide();
        markers[1]->hide();

        if (selectedPoints.size() == 1)
        {
            double x = selectedPoints[0].x();
            markers[0]->setXValue(x);
            markers[0]->setLabel(QString("    x1=%1").arg(x));
            markers[0]->show();
        }
        else if (selectedPoints.size() > 1)
        {
            auto interval = getInterval();
            double x1 = interval.minValue();
            double x2 = interval.maxValue();

            markers[0]->setXValue(x1);
            markers[0]->setLabel(QString("    x1=%1").arg(x1));
            markers[0]->show();

            markers[1]->setXValue(x2);
            markers[1]->setLabel(QString("    x2=%1").arg(x2));
            markers[1]->show();

            zoneItem->setInterval(interval);
            zoneItem->show();
        }

        plot->replot(); // force replot to make marker movement smooth
    }
};

NewIntervalPicker::NewIntervalPicker(QwtPlot *plot)
    : PlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                 QwtPicker::NoRubberBand,
                 QwtPicker::AlwaysOff,
                 plot->canvas())
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->plot = plot;
    d->markers[0] = make_position_marker();
    d->markers[1] = make_position_marker();

    for (auto &marker: d->markers)
    {
        marker->attach(d->plot);
        marker->hide();
    }

    d->zoneItem = new QwtPlotZoneItem;

    auto zoneBrush = d->zoneItem->brush();
    zoneBrush.setStyle(Qt::DiagCrossPattern);
    d->zoneItem->setBrush(zoneBrush);

    d->zoneItem->attach(d->plot);
    d->zoneItem->hide();

    setStateMachine(new AutoBeginClickPointMachine);

    connect(this, qOverload<const QPointF &>(&QwtPlotPicker::selected),
            this, &NewIntervalPicker::onPointSelected);

    connect(this, qOverload<const QPointF &>(&QwtPlotPicker::moved),
            this, &NewIntervalPicker::onPointMoved);

    connect(this, qOverload<const QPointF &>(&QwtPlotPicker::appended),
            this, &NewIntervalPicker::onPointAppended);
}

NewIntervalPicker::~NewIntervalPicker()
{
    qDebug() << __PRETTY_FUNCTION__ << this;
}

void NewIntervalPicker::reset()
{
    d->selectedPoints.clear();
    d->updateMarkersAndZone();
    PlotPicker::reset();
}

void NewIntervalPicker::cancel()
{
    reset();
    emit canceled();
}

void NewIntervalPicker::onPointSelected(const QPointF &p)
{
    qDebug() << __PRETTY_FUNCTION__ << "#points=" << d->selectedPoints.size()
        << "x=" << p.x();

    // Update the latest point with the selection position
    if (d->selectedPoints.size())
        d->selectedPoints.back() = p;

    if (d->selectedPoints.size() == 2)
        emit intervalSelected(d->getInterval());

    d->updateMarkersAndZone();
}

void NewIntervalPicker::onPointMoved(const QPointF &p)
{
    //qDebug() << __PRETTY_FUNCTION__ << "#points=" << d->selectedPoints.size()
    //    << "x=" << p.x();

    // Update the latest point with the move position
    if (d->selectedPoints.size())
        d->selectedPoints.back() = p;

    d->updateMarkersAndZone();
}

void NewIntervalPicker::onPointAppended(const QPointF &p)
{
    qDebug() << __PRETTY_FUNCTION__ << "#points=" << d->selectedPoints.size()
        << "x=" << p.x();

    // start/restart the interval selection process
    if (d->selectedPoints.size() >= 2)
        d->selectedPoints.clear();

    d->selectedPoints.push_back(p);
    d->updateMarkersAndZone();
}

void NewIntervalPicker::transition(const QEvent *event)
{
    switch (event->type())
    {
        case QEvent::MouseButtonRelease:
            if (mouseMatch(QwtEventPattern::MouseSelect2,
                           static_cast<const QMouseEvent *>(event)))
            {
                qDebug() << __PRETTY_FUNCTION__ << "canceled";
                cancel();
            }
            else
            {
                PlotPicker::transition(event);
            }
            break;

        default:
            PlotPicker::transition(event);
            break;
    }
}

struct IntervalEditorPicker::Private
{
    IntervalEditorPicker *q;
    QwtPlot *plot;
    // Ownership of the markers and the zone item goes to the QwtPlot instance.
    std::array<QwtPlotMarker *, 2> markers;
    QwtPlotZoneItem * zoneItem;
    QVector<QPointF> selectedPoints;
    int draggingPointIndex = -1;

    QwtInterval getInterval() const
    {
        if (selectedPoints.size() != 2)
            return {};

        return QwtInterval(selectedPoints[0].x(), selectedPoints[1].x()).normalized();
    }

    void updateMarkersAndZone()
    {
        zoneItem->hide();
        markers[0]->hide();
        markers[1]->hide();

        if (getInterval().isValid())
        {
            qDebug() << __PRETTY_FUNCTION__ << "got valid interval, showing plot items";
            auto interval = getInterval();
            double x1 = interval.minValue();
            double x2 = interval.maxValue();

            markers[0]->setXValue(x1);
            markers[0]->setLabel(QString("    x1=%1").arg(x1));
            markers[0]->show();

            markers[1]->setXValue(x2);
            markers[1]->setLabel(QString("    x2=%1").arg(x2));
            markers[1]->show();

            zoneItem->setInterval(interval);
            zoneItem->show();
        }

        plot->replot(); // force replot to make marker movement smooth
    }

    // Returns the point index (0 or 1) if pixelX is within
    // CanStartDragDistancePixels
    int getClosestPointIndex(int pixelX)
    {
        auto interval = getInterval();

        if (interval.isValid())
        {
            int iMinPixel = q->transform({ interval.minValue(), 0.0 }).x();
            int iMaxPixel = q->transform({ interval.maxValue(), 0.0 }).x();

            if (std::abs(pixelX - iMinPixel) < CanStartDragDistancePixels)
                return 0;
            else if (std::abs(pixelX - iMaxPixel) < CanStartDragDistancePixels)
                return 1;
        }

        return -1;
    }
};

IntervalEditorPicker::IntervalEditorPicker(QwtPlot *plot)
    : PlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                 QwtPicker::NoRubberBand,
                 QwtPicker::AlwaysOff,
                 plot->canvas())
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->plot = plot;
    d->markers[0] = make_position_marker();
    d->markers[1] = make_position_marker();

    for (auto &marker: d->markers)
    {
        marker->attach(d->plot);
        marker->hide();
    }

    d->zoneItem = new QwtPlotZoneItem;

    auto zoneBrush = d->zoneItem->brush();
    zoneBrush.setStyle(Qt::DiagCrossPattern);
    d->zoneItem->setBrush(zoneBrush);

    d->zoneItem->attach(d->plot);
    d->zoneItem->hide();

    setStateMachine(new AutoBeginClickPointMachine);

    connect(this, qOverload<const QPointF &>(&QwtPlotPicker::moved),
            this, &IntervalEditorPicker::onPointMoved);
}

IntervalEditorPicker::~IntervalEditorPicker()
{
    qDebug() << __PRETTY_FUNCTION__ << this;
}

void IntervalEditorPicker::setInterval(const QwtInterval &interval)
{
    if (interval.isValid())
        d->selectedPoints = { { interval.minValue(), 0.0 }, { interval.maxValue(), 0.0 } };
    else
        d->selectedPoints.clear();

    d->updateMarkersAndZone();
}

void IntervalEditorPicker::reset()
{
    d->selectedPoints.clear();
    d->updateMarkersAndZone();
    PlotPicker::reset();
}

void IntervalEditorPicker::widgetMousePressEvent(QMouseEvent *ev)
{
    if (mouseMatch(QwtEventPattern::MouseSelect1, static_cast<const QMouseEvent *>(ev))
        && d->getInterval().isValid())
    {
        qDebug() << __PRETTY_FUNCTION__ << "draggingPointIndex=" << d->draggingPointIndex;
        d->draggingPointIndex = d->getClosestPointIndex(ev->pos().x());
    }
    else
        PlotPicker::widgetMousePressEvent(ev);
}

void IntervalEditorPicker::widgetMouseReleaseEvent(QMouseEvent *ev)
{
    if (!mouseMatch(QwtEventPattern::MouseSelect1, static_cast<const QMouseEvent *>(ev)))
        return;

    if (d->draggingPointIndex < 0)
        return;

    d->draggingPointIndex = -1;
    std::sort(d->selectedPoints.begin(), d->selectedPoints.end(),
              [] (const auto &a, const auto &b) { return a.x() < b.x(); });
}

void IntervalEditorPicker::widgetMouseMoveEvent(QMouseEvent *ev)
{
    if (d->getInterval().isValid() && d->draggingPointIndex < 0)
    {
        if (d->getClosestPointIndex(ev->pos().x()) >= 0)
            canvas()->setCursor(Qt::SplitHCursor);
        else
            canvas()->setCursor(Qt::CrossCursor);
    }
    else
        PlotPicker::widgetMouseMoveEvent(ev);
}

void IntervalEditorPicker::onPointMoved(const QPointF &p)
{
    qDebug() << __PRETTY_FUNCTION__ << p;
    if (d->draggingPointIndex >= 0)
    {
        assert(d->draggingPointIndex < d->selectedPoints.size());
        d->selectedPoints[d->draggingPointIndex] = p;
        d->updateMarkersAndZone();
        emit intervalModified(d->getInterval());
    }
}

QList<QwtPickerMachine::Command> ImprovedPickerPolygonMachine::transition(
    const QwtEventPattern &eventPattern, const QEvent *event)
{
    auto cmdList = QwtPickerPolygonMachine::transition(eventPattern, event);

    if (event->type() == QEvent::MouseButtonPress)
    {
        if (eventPattern.mouseMatch(
                QwtEventPattern::MouseSelect3,
                static_cast<const QMouseEvent *>(event)))
            {
                cmdList += Remove;
            }
    }

    return cmdList;
}

bool is_linear_axis_scale(const QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<const QwtLinearScaleEngine *>(plot->axisScaleEngine(axis));
}

bool is_logarithmic_axis_scale(const QwtPlot *plot, QwtPlot::Axis axis)
{
    return dynamic_cast<const QwtLogScaleEngine *>(plot->axisScaleEngine(axis));
}

PlotAxisScaleChanger::PlotAxisScaleChanger(QwtPlot *plot, QwtPlot::Axis axis)
    : QObject(plot)
    , m_plot(plot)
    , m_axis(axis)
{
}

bool PlotAxisScaleChanger::isLinear() const
{
    return is_linear_axis_scale(m_plot, m_axis);
}

bool PlotAxisScaleChanger::isLogarithmic() const
{
    return is_logarithmic_axis_scale(m_plot, m_axis);
}

void PlotAxisScaleChanger::setLinear()
{
    if (!isLinear())
    {
        m_plot->setAxisScaleEngine(m_axis, new QwtLinearScaleEngine);
        m_plot->setAxisAutoScale(m_axis, true);
    }
}

void PlotAxisScaleChanger::setLogarithmic()
{
    if (!isLogarithmic())
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        m_plot->setAxisScaleEngine(m_axis, scaleEngine);
    }
}

void setup_axis_scale_changer(PlotWidget *w, QwtPlot::Axis axis, const QString &axisText)
{
    auto scaleChanger = new PlotAxisScaleChanger(w->getPlot(), axis);
    auto combo = new QComboBox;
    combo->addItem("Lin");
    combo->addItem("Log");

    auto container = make_vbox_container(axisText, combo, 2, -2).container.release();
    w->getToolBar()->addWidget(container);

    QObject::connect(
        combo, qOverload<int>(&QComboBox::currentIndexChanged),
        w, [w, scaleChanger] (int index)
        {
            if (index == 0)
                scaleChanger->setLinear();
            else
                scaleChanger->setLogarithmic();
            w->replot();
        });
}

}
