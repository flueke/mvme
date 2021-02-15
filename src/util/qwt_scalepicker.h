// This file is part of GoldenCheeta (GPLv2)
// Source: https://github.com/GoldenCheetah/GoldenCheetah/tree/931ce07e9976237117ddfddf7aa4d5e33ed91024/qwt/examples/event_filter
#include <qobject.h>
#include <qrect.h>

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
