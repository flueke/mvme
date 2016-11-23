#ifndef _SCROLLZOOMER_H
#define _SCROLLZOOMER_H

#include <qglobal.h>
#include <qwt_plot_zoomer.h>
#include <qwt_plot.h>
#include <qwt_scale_map.h>

class ScrollData;
class ScrollBar;

class ScrollZoomer: public QwtPlotZoomer
{
    Q_OBJECT


signals:
    void mouseCursorMovedTo(QPointF);
    void mouseCursorLeftPlot();

public:
    enum ScrollBarPosition
    {
        AttachedToScale,
        OppositeToScale
    };

    ScrollZoomer( QWidget * );
    virtual ~ScrollZoomer();

    ScrollBar *horizontalScrollBar() const;
    ScrollBar *verticalScrollBar() const;

    void setHScrollBarMode( Qt::ScrollBarPolicy );
    void setVScrollBarMode( Qt::ScrollBarPolicy );

    Qt::ScrollBarPolicy vScrollBarMode () const;
    Qt::ScrollBarPolicy hScrollBarMode () const;

    void setHScrollBarPosition( ScrollBarPosition );
    void setVScrollBarPosition( ScrollBarPosition );

    ScrollBarPosition hScrollBarPosition() const;
    ScrollBarPosition vScrollBarPosition() const;

    QWidget* cornerWidget() const;
    virtual void setCornerWidget( QWidget * );

    virtual bool eventFilter( QObject *, QEvent * );

    virtual void rescale();
    quint32 getLowborder();
    quint32 getHiborder();

    void setConversionX(const QwtScaleMap &map)
    {
        m_conversionX = map;
    }

    void setConversionY(const QwtScaleMap &map)
    {
        m_conversionY = map;
    }


protected:
    virtual ScrollBar *scrollBar( Qt::Orientation );
    virtual void updateScrollBars();
    virtual void layoutScrollBars( const QRect & );

    virtual void widgetMouseMoveEvent(QMouseEvent *event);
    virtual void widgetLeaveEvent( QEvent * );

    // from QwtPlotPicker
    virtual QwtText trackerTextF( const QPointF & ) const override;

private Q_SLOTS:
    void scrollBarMoved( Qt::Orientation o, double min, double max );

private:
    bool needScrollBar( Qt::Orientation ) const;
    int oppositeAxis( int ) const;

    QWidget *d_cornerWidget;

    ScrollData *d_hScrollData;
    ScrollData *d_vScrollData;

    bool d_inZoom;
    bool d_alignCanvasToScales[ QwtPlot::axisCnt ];
    QwtScaleMap m_conversionX;
    QwtScaleMap m_conversionY;
};

#endif
