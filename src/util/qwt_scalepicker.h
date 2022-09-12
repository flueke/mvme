#ifndef __MVME2_SRC_UTIL_QWT_SCALEPICKER_H_
#define __MVME2_SRC_UTIL_QWT_SCALEPICKER_H_

// This file is part of GoldenCheeta (GPLv2)
// Source: https://github.com/GoldenCheetah/GoldenCheetah/tree/931ce07e9976237117ddfddf7aa4d5e33ed91024/qwt/examples/event_filter

#include <QObject>
#include <QRect>
#include <qwt_scale_widget.h>
#include <qwt_plot.h>

class QwtPlot;
class QwtScaleWidget;

class ScalePicker: public QObject
{
    Q_OBJECT
public:
    ScalePicker( QwtPlot *plot );
    virtual bool eventFilter( QObject *, QEvent * );

Q_SIGNALS:
    void clicked( int axis, double value );

private:
    void mouseClicked( const QwtScaleWidget *, const QPoint & );
    QRect scaleRect( const QwtScaleWidget * ) const;
};

#endif // __MVME2_SRC_UTIL_QWT_SCALEPICKER_H_
