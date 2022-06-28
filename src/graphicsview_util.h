#ifndef __MVME_GRAPHICSVIEW_UTIL_H__
#define __MVME_GRAPHICSVIEW_UTIL_H__

#include <QObject>

class QGraphicsView;

class MouseWheelZoomer: public QObject
{
    Q_OBJECT

    public:
       using QObject::QObject;

       // Convenience constructor installing the MouseWheelZoomer on the view.
       MouseWheelZoomer(QGraphicsView *view, QObject *parent = nullptr);

       bool eventFilter(QObject *watched, QEvent *event) override;
};

void scale_view(
    QGraphicsView *view, double scaleFactor,
    double zoomOutLimit = 0.25, double zoomInLimit = 10);

#endif /* __MVME_GRAPHICSVIEW_UTIL_H__ */