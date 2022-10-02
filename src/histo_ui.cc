#include "histo_ui.h"

#include <array>
#include <memory>
#include <QBoxLayout>
#include <QDebug>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QStatusBar>
#include <QToolBar>
#include <QVector2D>
#include <qwt_plot_marker.h>
#include <qwt_plot_shapeitem.h>
#include <qwt_plot_zoneitem.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_map.h>
#include <spdlog/spdlog.h>

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

    static const int CanStartDragDistancePixels = 6;
}

PlotPicker::PlotPicker(QWidget *canvas)
    : QwtPlotPicker(canvas)
{
    qDebug() << __PRETTY_FUNCTION__ << this;
#ifdef Q_OS_WIN
    bool b = connect(this, SIGNAL(removed(const QPoint &)),
                     this, SLOT(onPointRemoved(const QPoint &)));
#else
    bool b = connect(this, qOverload<const QPoint &>(&QwtPicker::removed),
                     this, &PlotPicker::onPointRemoved);
#endif
    assert(b);
}

PlotPicker::PlotPicker(int xAxis, int yAxis,
                       RubberBand rubberBand,
                       DisplayMode trackerMode,
                       QWidget *canvas)
    : QwtPlotPicker(xAxis, yAxis, rubberBand, trackerMode, canvas)
{
    qDebug() << __PRETTY_FUNCTION__ << this;
#ifdef Q_OS_WIN
    bool b = connect(this, SIGNAL(removed(const QPoint &)),
                     this, SLOT(onPointRemoved(const QPoint &)));
#else
    bool b = connect(this, qOverload<const QPoint &>(&QwtPicker::removed),
                     this, &PlotPicker::onPointRemoved);
#endif
    assert(b);
}

void PlotPicker::onPointRemoved(const QPoint &p)
{
    emit removed(invTransform(p));
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

    bool b = false;

#ifdef Q_OS_WIN
    b = connect(this, SIGNAL(selected(const QPointF &)),
                this, SLOT(onPointSelected(const QPointF &)));
    assert(b);

    b = connect(this, SIGNAL(moved(const QPointF &)),
                this, SLOT(onPointMoved(const QPointF &)));
    assert(b);

    b = connect(this, SIGNAL(appended(const QPointF &)),
                this, SLOT(onPointAppended(const QPointF &)));
    assert(b);
#else
    b = connect(this, qOverload<const QPointF &>(&QwtPlotPicker::selected),
                this, &NewIntervalPicker::onPointSelected);
    assert(b);

    b = connect(this, qOverload<const QPointF &>(&QwtPlotPicker::moved),
                this, &NewIntervalPicker::onPointMoved);
    assert(b);

    b = connect(this, qOverload<const QPointF &>(&QwtPlotPicker::appended),
                this, &NewIntervalPicker::onPointAppended);
    assert(b);
#endif
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
    int dragPointIndex_ = -1;

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

            if (std::abs(pixelX - iMinPixel) <= CanStartDragDistancePixels)
                return 0;
            else if (std::abs(pixelX - iMaxPixel) <= CanStartDragDistancePixels)
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

    bool b = false;

#ifdef Q_OS_WIN
    b = connect(this, SIGNAL(moved(const QPointF &)),
                this, SLOT(onPointMoved(const QPointF &)));
    assert(b);
#else
    b = connect(this, qOverload<const QPointF &>(&QwtPlotPicker::moved),
                this, &IntervalEditorPicker::onPointMoved);
    assert(b);
#endif
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
        qDebug() << __PRETTY_FUNCTION__ << "dragPointIndex_=" << d->dragPointIndex_;
        d->dragPointIndex_ = d->getClosestPointIndex(ev->pos().x());
    }
    else
        PlotPicker::widgetMousePressEvent(ev);
}

void IntervalEditorPicker::widgetMouseReleaseEvent(QMouseEvent *ev)
{
    if (!mouseMatch(QwtEventPattern::MouseSelect1, static_cast<const QMouseEvent *>(ev)))
        return;

    if (d->dragPointIndex_ < 0)
        return;

    d->dragPointIndex_ = -1;
    std::sort(d->selectedPoints.begin(), d->selectedPoints.end(),
              [] (const auto &a, const auto &b) { return a.x() < b.x(); });
}

void IntervalEditorPicker::widgetMouseMoveEvent(QMouseEvent *ev)
{
    if (d->getInterval().isValid() && d->dragPointIndex_ < 0)
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
    if (d->dragPointIndex_ >= 0)
    {
        assert(d->dragPointIndex_ < d->selectedPoints.size());
        d->selectedPoints[d->dragPointIndex_] = p;
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

namespace
{

// Returns the shortest distance from the point p to the line l. Fills cp with
// the coordinates of the closest point.
inline double distance_to_line(QPointF p, QLineF l, QPointF &cp)
{
    // line is 'ab', 'ap' is a to p
    QVector2D ab { l.p2() - l.p1() };
    QVector2D ap { p - l.p1() };
    auto proj = QVector2D::dotProduct(ap, ab);
    auto abLen2 = ab.lengthSquared();
    auto d = proj / abLen2;

    if (d <= 0.0)
        cp = l.p1();
    else if (d >= 1.0)
        cp = l.p2();
    else
        cp = l.pointAt(d);

    auto result = QVector2D{p}.distanceToPoint(QVector2D{cp});
    return result;
}

// Helper structure for determining the closest edge of a polygon to a given
// point.
struct EdgeIndexesAndDistance
{
    std::pair<int, int> indexes = { -1, -1 }; // indexes into the polygon
    double distance = {}; // distance from the given point to the edge
    QPointF closestPoint = {}; // coordinates of the closest point on the polygon edge

    inline bool isValid() const
    {
        return indexes.first >= 0;
    }
};

// Calculates the distance from p to each of the polygon edges. Returns info for
// the shortest distance found.
inline EdgeIndexesAndDistance closest_edge(QPointF p, const QPolygonF &poly)
{

    std::vector<EdgeIndexesAndDistance> distances;
    distances.reserve(poly.size());

    for (auto it_p1 = std::begin(poly), it_p2 = std::begin(poly) + 1;
         it_p2 < std::end(poly);
         ++it_p1, ++it_p2)
    {
        EdgeIndexesAndDistance ed;
        ed.indexes = { it_p1 - std::begin(poly), it_p2 - std::begin(poly) };
        ed.distance = distance_to_line(p, { *it_p1 , *it_p2 }, ed.closestPoint);
        distances.emplace_back(ed);
    }

#if 0
    qDebug() << ">>> p =" << p;

    std::for_each(std::begin(distances), std::end(distances),
                  [](const auto &ed)
                  {
                      qDebug() << "i1" << ed.indexes.first << ", i2" << ed.indexes.second << ", distance =" << ed.distance;
                  });

    qDebug() << "<<<";
#endif

    std::sort(std::begin(distances), std::end(distances),
              [] (const auto &ed1, const auto &ed2)
              {
                return ed1.distance < ed2.distance;
              });

    EdgeIndexesAndDistance ed;

    if (!distances.empty())
        ed = distances[0];

#if 0
    qDebug() << "<<< i1" << ed.indexes.first << ", i2" << ed.indexes.second << ", distance =" << ed.distance << ", closestPoint =" << ed.closestPoint;
#endif

    return ed;
}

}

struct PolygonEditorPicker::Private
{
    enum class State
    {
        Default,
        DragPoint,
        DragEdge,
        PanPolygon,
    };

    PolygonEditorPicker *q;
    QwtPlot *plot;
    State state_ = State::Default;
    QwtPlotShapeItem *edgeHighlight; // ownership goes to QwtPlot
    QPolygonF poly_; // the polygon being edited
    QPolygonF pixelPoly_; // the polygon in pixel coordinates. updated in pixelPoly().
    int dragPointIndex_ = -1;
    std::pair<int, int> dragEdgePointIndexes_ = { -1, -1 };
    std::pair<QPointF, QPointF> dragEdgeStartPolyPoints_;
    QPointF dragEdgeStartPoint_;
    QPolygonF panStartPoly_;
    QPointF panStartPoint_;

    // Returns the index of the first polygon point that is close enough to the
    // given pixel coordinates so that it can be used for mouse drag operations.
    // Returns -1 if there is no such point.
    int getPointIndexInDragRange(const QPoint &pixelPoint)
    {
        for (int i=0; i<poly_.size(); ++i)
        {
            auto p = q->transform(poly_[i]); // plot to pixel coordinates

            auto dx = std::abs(pixelPoint.x() - p.x());
            auto dy = std::abs(pixelPoint.y() - p.y());

            if (dx <= CanStartDragDistancePixels && dy <= CanStartDragDistancePixels)
                return i;
        }
        return -1;
    }

    // Returns the polygon converted to pixel coordinates.
    const QPolygonF &pixelPoly()
    {
        pixelPoly_.clear();
        pixelPoly_.reserve(poly_.size());

        std::transform(std::begin(poly_), std::end(poly_), std::back_inserter(pixelPoly_),
                       [this](const auto &p)
                       { return q->transform(p); });

        return pixelPoly_;
    }

    void movePolyPoint(int pointIndex, const QPointF &p)
    {
        if (0 <= pointIndex && pointIndex < poly_.size())
        {
            poly_[pointIndex] = p;

            // when moving the first point also move the last one
            if (pointIndex == 0)
                poly_[poly_.size()-1] = p;

            // when moving the last point also move the first one
            if (pointIndex == poly_.size()-1)
                poly_[0] = p;
        }
    }

    void removePolyPoint(int pointIndex)
    {
        auto before = poly_;
        auto &poly = poly_;

        if (0 <= pointIndex  && pointIndex < poly.size())
        {
            if (pointIndex == 0)
            {
                poly.pop_front();
                // To make sure the poly is closed when removing the first point,
                // move the last point to the new front of the poly.
                if (poly.size())
                    poly.back() = poly.front();
            }
            else if (pointIndex == poly.size() - 1)
            {
                poly.pop_back();
                // Close the poly by moving the first point to the new back of the poly
                if (poly.size())
                    poly.front() = poly.back();
            }
            else
                poly.remove(pointIndex);

            if (poly.size() <= 2)
            {
                // only two points remain which should be at the same coordinates
                poly.clear();
            }
        }

        const auto &after = poly;
        (void) before;
        (void) after;
        //qDebug() << "pointIndex =" << pointIndex;
        //qDebug() << "before" << before << ", size =" << before.size();
        //qDebug() << "after" << after << ", size =" << after.size();
    }
};

PolygonEditorPicker::PolygonEditorPicker(QwtPlot *plot)
    : PlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                 QwtPicker::NoRubberBand,
                 QwtPicker::AlwaysOff,
                 plot->canvas())
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->plot = plot;

    QPen pen(Qt::red, 5.0);
    d->edgeHighlight = new QwtPlotShapeItem;
    d->edgeHighlight->setPen(pen);
    d->edgeHighlight->attach(d->plot);
    d->edgeHighlight->hide();

    setStateMachine(new AutoBeginClickPointMachine);

    bool b = false;
#ifdef Q_OS_WIN
    b = connect(this, SIGNAL(moved(const QPointF &)),
                this, SLOT(onPointMoved(const QPointF &)));
    assert(b);
#else
    b = connect(this, qOverload<const QPointF &>(&QwtPlotPicker::moved),
                this, &PolygonEditorPicker::onPointMoved);
    assert(b);
#endif
}

PolygonEditorPicker::~PolygonEditorPicker()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void PolygonEditorPicker::setPolygon(const QPolygonF &poly)
{
    d->poly_ = poly;
}

void PolygonEditorPicker::reset()
{
    d->poly_ = {};
    d->state_ = Private::State::Default;
    d->edgeHighlight->hide();
    PlotPicker::reset();
}

void PolygonEditorPicker::widgetMouseMoveEvent(QMouseEvent *ev)
{
    using State = Private::State;

    d->edgeHighlight->hide();

    if (d->state_ == State::Default)
    {
        // Note: the distance calculations are done in pixel coordinates.
        auto ed = closest_edge(ev->pos(), d->pixelPoly());
        //qDebug() << "i1" << ed.indexes.first << ", i2" << ed.indexes.second << ", distance =" << ed.distance;

        // Point drag detection
        if (d->getPointIndexInDragRange(ev->pos()) >= 0)
        {
            // Arrows top, left, right, bottom
            // Can start to drag this point with mouse button 1.
            canvas()->setCursor(Qt::SizeAllCursor);
        }
        // Edge drag detection
        else if (ed.isValid() && ed.distance <= CanStartDragDistancePixels)
        {
            auto p1 = d->poly_[ed.indexes.first];
            auto p2 = d->poly_[ed.indexes.second];
            auto polyLine = QPolygonF() << p1 << p2;
            d->edgeHighlight->setPolygon(polyLine);
            d->edgeHighlight->show();

            // Note: angle calculation is done in pixel coordinates as the
            // plot x and y-axes can have different scales.
            QLineF line{transform(p1), transform(p2)};
            auto angle = line.angle();

            if ((0.0 <= angle && angle < 45.0)
                || (135.0 <= angle && angle < 225.0)
                || (315.0 <= angle && angle < 360.0))
            {
                canvas()->setCursor(Qt::SizeVerCursor);
            }
            else
                canvas()->setCursor(Qt::SizeHorCursor);
        }
        // Pan detection
        else if (d->poly_.containsPoint(invTransform(ev->pos()), Qt::WindingFill))
        {
            canvas()->setCursor(Qt::OpenHandCursor);
        }
        else
            canvas()->setCursor(Qt::CrossCursor);
    }

    PlotPicker::widgetMouseMoveEvent(ev);
    d->plot->replot();
}

void PolygonEditorPicker::widgetMousePressEvent(QMouseEvent *ev)
{
    d->edgeHighlight->hide();

    const bool mb1 = mouseMatch(QwtEventPattern::MouseSelect1, static_cast<const QMouseEvent *>(ev));
    const bool mb2 = mouseMatch(QwtEventPattern::MouseSelect2, static_cast<const QMouseEvent *>(ev));

    using State = Private::State;

    d->dragPointIndex_ = d->getPointIndexInDragRange(ev->pos());
    const auto ed = closest_edge(ev->pos(), d->pixelPoly());

    if (d->state_ == State::Default && mb1)
    {
        if (d->dragPointIndex_ >= 0)
        {
            d->state_ = State::DragPoint;
        }
        else if (ed.isValid() && ed.distance <= CanStartDragDistancePixels)
        {
            d->dragEdgePointIndexes_ = ed.indexes;
            d->dragEdgeStartPoint_ = invTransform(ev->pos());
            d->dragEdgeStartPolyPoints_.first = d->poly_[ed.indexes.first];
            d->dragEdgeStartPolyPoints_.second = d->poly_[ed.indexes.second];
            d->state_ = State::DragEdge;
        }
        else if (d->poly_.containsPoint(invTransform(ev->pos()), Qt::WindingFill))
        {
            canvas()->setCursor(Qt::ClosedHandCursor);
            d->panStartPoly_ = d->poly_;
            d->panStartPoint_ = invTransform(ev->pos());
            d->state_ = State::PanPolygon;
        }
    }
    else if (d->state_ == State::Default && mb2)
    {
        if (d->dragPointIndex_ >= 0)
        {
            auto pointIndex = d->dragPointIndex_;
            QMenu menu;
            menu.addAction(QIcon::fromTheme("edit-delete"), "Remove Point", this, [this, pointIndex]
                {
                    d->removePolyPoint(pointIndex);
                    d->plot->replot();
                    emit polygonModified(d->poly_);
                });
            menu.exec(d->plot->mapToGlobal(ev->pos()));
        }
        else if (ed.isValid() && ed.distance <= CanStartDragDistancePixels)
        {
            QMenu menu;
            menu.addAction(QIcon::fromTheme("list-add"), "Insert Point", this, [this, ed]
                {
                    d->poly_.insert(ed.indexes.second, invTransform(ed.closestPoint.toPoint()));
                    d->plot->replot();
                    emit polygonModified(d->poly_);
                });
            menu.exec(d->plot->mapToGlobal(ev->pos()));
        }
    }
    else
        PlotPicker::widgetMousePressEvent(ev);

    widgetMouseMoveEvent(ev); // to update the state of plot items and schedule a replot
}

void PolygonEditorPicker::widgetMouseReleaseEvent(QMouseEvent *ev)
{
    if (!mouseMatch(QwtEventPattern::MouseSelect1, static_cast<const QMouseEvent *>(ev)))
        return;

    d->state_ = Private::State::Default;

    widgetMouseMoveEvent(ev); // to update the state of plot items and schedule a replot
}

void PolygonEditorPicker::onPointMoved(const QPointF &p)
{
    using State = Private::State;

    if (d->state_ == State::DragPoint)
    {
        d->movePolyPoint(d->dragPointIndex_, p);
        emit polygonModified(d->poly_);
    }
    else if (d->state_ == State::DragEdge)
    {
        auto delta = p - d->dragEdgeStartPoint_;
        d->movePolyPoint(d->dragEdgePointIndexes_.first, d->dragEdgeStartPolyPoints_.first + delta);
        d->movePolyPoint(d->dragEdgePointIndexes_.second, d->dragEdgeStartPolyPoints_.second + delta);
        emit polygonModified(d->poly_);
    }
    else if (d->state_ == State::PanPolygon)
    {
        auto delta = p - d->panStartPoint_;
        d->poly_ = d->panStartPoly_.translated(delta);
        emit polygonModified(d->poly_);
    }
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
