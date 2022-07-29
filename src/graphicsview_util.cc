#include "graphicsview_util.h"

#include <QGraphicsView>
#include <QWheelEvent>
#include <cmath>


MouseWheelZoomer::MouseWheelZoomer(QGraphicsView *view, QObject *parent)
    : QObject(parent)
{
    view->installEventFilter(this);
}

bool MouseWheelZoomer::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Wheel)
        return false;

    auto view = qobject_cast<QGraphicsView *>(watched);

    if (!view)
        return false;

    auto wheelEvent = static_cast<QWheelEvent *>(event);

    auto keyMods = wheelEvent->modifiers();
    double divisor = 300.0;

    if (keyMods & Qt::ControlModifier)
        divisor *= 3.0;
    else if (keyMods & Qt::ShiftModifier)
        divisor /= 3.0;

    scale_view(view, std::pow(2.0, wheelEvent->delta() / divisor));
    return true;
}

void scale_view(
    QGraphicsView *view, qreal scaleFactor,
    double zoomOutLimit, double zoomInLimit)
{
    qreal factor = view->transform().scale(scaleFactor, scaleFactor).mapRect(QRectF(0, 0, 1, 1)).width();
    if (factor < zoomOutLimit || factor > zoomInLimit)
        return;

    view->scale(scaleFactor, scaleFactor);
}