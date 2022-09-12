#ifndef __MVME_GRAPHICSVIEW_UTIL_H__
#define __MVME_GRAPHICSVIEW_UTIL_H__

#include <QObject>
#include "libmvme_export.h"

class QGraphicsView;

LIBMVME_EXPORT void scale_view(
    QGraphicsView *view, double scaleFactor,
    double zoomOutLimit = 0.25, double zoomInLimit = 10);


class LIBMVME_EXPORT MouseWheelZoomer: public QObject
{
    Q_OBJECT

    public:
       using QObject::QObject;

       bool eventFilter(QObject *watched, QEvent *event) override;
};

class LIBMVME_EXPORT FitInViewOnResizeFilter: public QObject
{
    Q_OBJECT

    public:
        using QObject::QObject;

        bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif /* __MVME_GRAPHICSVIEW_UTIL_H__ */
