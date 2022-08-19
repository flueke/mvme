#ifndef __MVME_GRAPHICSVIEW_UTIL_H__
#define __MVME_GRAPHICSVIEW_UTIL_H__

#include <QObject>
#include "libmvme_export.h"

class QGraphicsView;

class LIBMVME_EXPORT MouseWheelZoomer: public QObject
{
    Q_OBJECT

    public:
       using QObject::QObject;

       // Convenience constructor installing the MouseWheelZoomer on the view.
       MouseWheelZoomer(QGraphicsView *view, QObject *parent = nullptr);

       bool eventFilter(QObject *watched, QEvent *event) override;
};

LIBMVME_EXPORT void scale_view(
    QGraphicsView *view, double scaleFactor,
    double zoomOutLimit = 0.25, double zoomInLimit = 10);

class LIBMVME_EXPORT FitInViewOnResizeFilter: public QObject
{
    Q_OBJECT

    public:
        using QObject::QObject;

        bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif /* __MVME_GRAPHICSVIEW_UTIL_H__ */