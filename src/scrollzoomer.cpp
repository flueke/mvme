/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <qevent.h>
#include <QPen>
#include <qwt_plot_canvas.h>
#include <qwt_plot_layout.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_picker_machine.h>
#include "scrollbar.h"
#include "scrollzoomer.h"

class ScrollData
{
public:
    ScrollData():
        scrollBar( NULL ),
        position( ScrollZoomer::OppositeToScale ),
        mode( Qt::ScrollBarAsNeeded )
    {
    }

    ~ScrollData()
    {
        delete scrollBar;
    }

    ScrollBar *scrollBar;
    ScrollZoomer::ScrollBarPosition position;
    Qt::ScrollBarPolicy mode;
};

ScrollZoomer::ScrollZoomer( QWidget *canvas ):
    QwtPlotZoomer( canvas ),
    d_cornerWidget( NULL ),
    d_hScrollData( NULL ),
    d_vScrollData( NULL ),
    d_inZoom( false )
{
    for ( int axis = 0; axis < QwtPlot::axisCnt; axis++ )
        d_alignCanvasToScales[ axis ] = false;

    if ( !canvas )
        return;

    d_hScrollData = new ScrollData;
    d_vScrollData = new ScrollData;

    setRubberBandPen( QColor( Qt::green ) );
    setTrackerPen( QColor( Qt::black ) );
    setTrackerMode( QwtPicker::ActiveOnly );
    setRubberBand( QwtPicker::RectRubberBand );

    // RightButton: zoom out by 1
    // Ctrl+RightButton: zoom out to full size
    // Ctrl+LeftButton: zoom in by 1

    // QwtPlotZoomer uses MouseSelect2 to zoom out fully
    setMousePattern( QwtEventPattern::MouseSelect2,
        Qt::RightButton, Qt::ControlModifier );

    // QwtPlotZoomer uses MouseSelect3 to zoom out once
    setMousePattern( QwtEventPattern::MouseSelect3,
        Qt::RightButton );

    // QwtPlotZoomer uses MouseSelect6 to zoom in to the next zoom stack level.
    setMousePattern( QwtEventPattern::MouseSelect6,
        Qt::LeftButton, Qt::ControlModifier );

}

ScrollZoomer::~ScrollZoomer()
{
    delete d_cornerWidget;
    delete d_vScrollData;
    delete d_hScrollData;
}

void ScrollZoomer::rescale()
{
    QwtScaleWidget *xScale = plot()->axisWidget( xAxis() );
    QwtScaleWidget *yScale = plot()->axisWidget( yAxis() );
    //qDebug("rescale in zoomer");
    if ( zoomRectIndex() <= 0 )
    {
        //qDebug("index <= 0: %d", zoomRectIndex());
        if ( d_inZoom )
        {
            //qDebug("end zoom");
            xScale->setMinBorderDist( 0, 0 );
            yScale->setMinBorderDist( 0, 0 );

            QwtPlotLayout *layout = plot()->plotLayout();

            for ( int axis = 0; axis < QwtPlot::axisCnt; axis++ )
                layout->setAlignCanvasToScale( axis, d_alignCanvasToScales[axis] );

            d_inZoom = false;
        }
    }
    else
    {
        //qDebug("index > 0: %d", zoomRectIndex());
        if ( !d_inZoom )
        {
            //qDebug("start/increase zoom");
            /*
             We set a minimum border distance.
             Otherwise the canvas size changes when scrolling,
             between situations where the major ticks are at
             the canvas borders (requiring extra space for the label)
             and situations where all labels can be painted below/top
             or left/right of the canvas.
             */
            int start, end;

            xScale->getBorderDistHint( start, end );
            xScale->setMinBorderDist( start, end );

            yScale->getBorderDistHint( start, end );
            yScale->setMinBorderDist( start, end );

            QwtPlotLayout *layout = plot()->plotLayout();
            for ( int axis = 0; axis < QwtPlot::axisCnt; axis++ )
            {
                d_alignCanvasToScales[axis] =
                    layout->alignCanvasToScale( axis );
            }

            layout->setAlignCanvasToScales( false );

            d_inZoom = true;
        }
    }
    //qDebug("width: %2.2f, height: %2.2f", zoomRect().width(), zoomRect().height());
    //qDebug("left: %2.2f, right: %2.2f",zoomRect().left(), zoomRect().right());
    QwtPlotZoomer::rescale();
    updateScrollBars();
}

quint32 ScrollZoomer::getLowborder()
{
    return (quint32)zoomRect().left();
}

quint32 ScrollZoomer::getHiborder()
{
    return (quint32)zoomRect().right();
}

ScrollBar *ScrollZoomer::scrollBar( Qt::Orientation orientation )
{
    ScrollBar *&sb = ( orientation == Qt::Vertical )
        ? d_vScrollData->scrollBar : d_hScrollData->scrollBar;

    if ( sb == NULL )
    {
        sb = new ScrollBar( orientation, canvas() );
        sb->hide();
        connect( sb,
            SIGNAL( valueChanged( Qt::Orientation, double, double ) ),
            SLOT( scrollBarMoved( Qt::Orientation, double, double ) ) );
    }
    return sb;
}

ScrollBar *ScrollZoomer::horizontalScrollBar() const
{
    return d_hScrollData->scrollBar;
}

ScrollBar *ScrollZoomer::verticalScrollBar() const
{
    return d_vScrollData->scrollBar;
}

void ScrollZoomer::setHScrollBarMode( Qt::ScrollBarPolicy mode )
{
    if ( hScrollBarMode() != mode )
    {
        d_hScrollData->mode = mode;
        updateScrollBars();
    }
}

void ScrollZoomer::setVScrollBarMode( Qt::ScrollBarPolicy mode )
{
    if ( vScrollBarMode() != mode )
    {
        d_vScrollData->mode = mode;
        updateScrollBars();
    }
}

Qt::ScrollBarPolicy ScrollZoomer::hScrollBarMode() const
{
    return d_hScrollData->mode;
}

Qt::ScrollBarPolicy ScrollZoomer::vScrollBarMode() const
{
    return d_vScrollData->mode;
}

void ScrollZoomer::setHScrollBarPosition( ScrollBarPosition pos )
{
    if ( d_hScrollData->position != pos )
    {
        d_hScrollData->position = pos;
        updateScrollBars();
    }
}

void ScrollZoomer::setVScrollBarPosition( ScrollBarPosition pos )
{
    if ( d_vScrollData->position != pos )
    {
        d_vScrollData->position = pos;
        updateScrollBars();
    }
}

ScrollZoomer::ScrollBarPosition ScrollZoomer::hScrollBarPosition() const
{
    return d_hScrollData->position;
}

ScrollZoomer::ScrollBarPosition ScrollZoomer::vScrollBarPosition() const
{
    return d_vScrollData->position;
}

void ScrollZoomer::setCornerWidget( QWidget *w )
{
    if ( w != d_cornerWidget )
    {
        if ( canvas() )
        {
            delete d_cornerWidget;
            d_cornerWidget = w;
            if ( d_cornerWidget->parent() != canvas() )
                d_cornerWidget->setParent( canvas() );

            updateScrollBars();
        }
    }
}

QWidget *ScrollZoomer::cornerWidget() const
{
    return d_cornerWidget;
}

bool ScrollZoomer::eventFilter( QObject *object, QEvent *event )
{
    if ( object == canvas() )
    {
        switch( event->type() )
        {
            case QEvent::Resize:
            {
                auto margins = canvas()->contentsMargins();

                QRect rect;
                rect.setSize( static_cast<QResizeEvent *>( event )->size() );
                rect.adjust( margins.left(), margins.top(), -margins.right(), -margins.bottom());

                layoutScrollBars( rect );
                break;
            }
            case QEvent::ChildRemoved:
            {
                const QObject *child =
                    static_cast<QChildEvent *>( event )->child();

                if ( child == d_cornerWidget )
                    d_cornerWidget = NULL;
                else if ( child == d_hScrollData->scrollBar )
                    d_hScrollData->scrollBar = NULL;
                else if ( child == d_vScrollData->scrollBar )
                    d_vScrollData->scrollBar = NULL;
                break;
            }
            default:
                break;
        }
    }
    return QwtPlotZoomer::eventFilter( object, event );
}

bool ScrollZoomer::needScrollBar( Qt::Orientation orientation ) const
{
    Qt::ScrollBarPolicy mode;
    double zoomMin, zoomMax, baseMin, baseMax;

    if ( orientation == Qt::Horizontal )
    {
        mode = d_hScrollData->mode;
        baseMin = zoomBase().left();
        baseMax = zoomBase().right();
        zoomMin = zoomRect().left();
        zoomMax = zoomRect().right();
    }
    else
    {
        mode = d_vScrollData->mode;
        baseMin = zoomBase().top();
        baseMax = zoomBase().bottom();
        zoomMin = zoomRect().top();
        zoomMax = zoomRect().bottom();
    }

    bool needed = false;
    switch( mode )
    {
        case Qt::ScrollBarAlwaysOn:
            needed = true;
            break;
        case Qt::ScrollBarAlwaysOff:
            needed = false;
            break;
        default:
        {
            if ( baseMin < zoomMin || baseMax > zoomMax )
                needed = true;
            break;
        }
    }
    return needed;
}

void ScrollZoomer::updateScrollBars()
{
    if ( !canvas() )
        return;

    const int xAxis = QwtPlotZoomer::xAxis();
    const int yAxis = QwtPlotZoomer::yAxis();

    int xScrollBarAxis = xAxis;
    if ( hScrollBarPosition() == OppositeToScale )
        xScrollBarAxis = oppositeAxis( xScrollBarAxis );

    int yScrollBarAxis = yAxis;
    if ( vScrollBarPosition() == OppositeToScale )
        yScrollBarAxis = oppositeAxis( yScrollBarAxis );


    QwtPlotLayout *layout = plot()->plotLayout();

    bool showHScrollBar = needScrollBar( Qt::Horizontal );
    if ( showHScrollBar )
    {
        ScrollBar *sb = scrollBar( Qt::Horizontal );
        sb->setPalette( plot()->palette() );
        sb->setInverted( !plot()->axisScaleDiv( xAxis ).isIncreasing() );
        sb->setBase( zoomBase().left(), zoomBase().right() );
        sb->moveSlider( zoomRect().left(), zoomRect().right() );

        if ( !sb->isVisibleTo( canvas() ) )
        {
            sb->show();
            layout->setCanvasMargin( layout->canvasMargin( xScrollBarAxis )
                + sb->extent(), xScrollBarAxis );
        }
    }
    else
    {
        if ( horizontalScrollBar() )
        {
            horizontalScrollBar()->hide();
            layout->setCanvasMargin( layout->canvasMargin( xScrollBarAxis )
                - horizontalScrollBar()->extent(), xScrollBarAxis );
        }
    }

    bool showVScrollBar = needScrollBar( Qt::Vertical );
    if ( showVScrollBar )
    {
        ScrollBar *sb = scrollBar( Qt::Vertical );
        sb->setPalette( plot()->palette() );
        sb->setInverted( plot()->axisScaleDiv( yAxis ).isIncreasing() );
        sb->setBase( zoomBase().top(), zoomBase().bottom() );
        sb->moveSlider( zoomRect().top(), zoomRect().bottom() );

        if ( !sb->isVisibleTo( canvas() ) )
        {
            sb->show();
            layout->setCanvasMargin( layout->canvasMargin( yScrollBarAxis )
                + sb->extent(), yScrollBarAxis );
        }
    }
    else
    {
        if ( verticalScrollBar() )
        {
            verticalScrollBar()->hide();
            layout->setCanvasMargin( layout->canvasMargin( yScrollBarAxis )
                - verticalScrollBar()->extent(), yScrollBarAxis );
        }
    }

    if ( showHScrollBar && showVScrollBar )
    {
        if ( d_cornerWidget == NULL )
        {
            d_cornerWidget = new QWidget( canvas() );
            d_cornerWidget->setAutoFillBackground( true );
            d_cornerWidget->setPalette( plot()->palette() );
        }
        d_cornerWidget->show();
    }
    else
    {
        if ( d_cornerWidget )
            d_cornerWidget->hide();
    }

    layoutScrollBars( canvas()->contentsRect() );
    plot()->updateLayout();
}

void ScrollZoomer::layoutScrollBars( const QRect &rect )
{
    int hPos = xAxis();
    if ( hScrollBarPosition() == OppositeToScale )
        hPos = oppositeAxis( hPos );

    int vPos = yAxis();
    if ( vScrollBarPosition() == OppositeToScale )
        vPos = oppositeAxis( vPos );

    ScrollBar *hScrollBar = horizontalScrollBar();
    ScrollBar *vScrollBar = verticalScrollBar();

    const int hdim = hScrollBar ? hScrollBar->extent() : 0;
    const int vdim = vScrollBar ? vScrollBar->extent() : 0;

    if ( hScrollBar && hScrollBar->isVisible() )
    {
        int x = rect.x();
        int y = ( hPos == QwtPlot::xTop )
            ? rect.top() : rect.bottom() - hdim + 1;
        int w = rect.width();

        if ( vScrollBar && vScrollBar->isVisible() )
        {
            if ( vPos == QwtPlot::yLeft )
                x += vdim;
            w -= vdim;
        }

        hScrollBar->setGeometry( x, y, w, hdim );
    }
    if ( vScrollBar && vScrollBar->isVisible() )
    {
        int pos = yAxis();
        if ( vScrollBarPosition() == OppositeToScale )
            pos = oppositeAxis( pos );

        int x = ( vPos == QwtPlot::yLeft )
            ? rect.left() : rect.right() - vdim + 1;
        int y = rect.y();

        int h = rect.height();

        if ( hScrollBar && hScrollBar->isVisible() )
        {
            if ( hPos == QwtPlot::xTop )
                y += hdim;

            h -= hdim;
        }

        vScrollBar->setGeometry( x, y, vdim, h );
    }
    if ( hScrollBar && hScrollBar->isVisible() &&
        vScrollBar && vScrollBar->isVisible() )
    {
        if ( d_cornerWidget )
        {
            QRect cornerRect(
                vScrollBar->pos().x(), hScrollBar->pos().y(),
                vdim, hdim );
            d_cornerWidget->setGeometry( cornerRect );
        }
    }
}

void ScrollZoomer::scrollBarMoved(
    Qt::Orientation o, double min, double max )
{
    Q_UNUSED( max );

    if ( o == Qt::Horizontal )
        moveTo( QPointF( min, zoomRect().top() ) );
    else
        moveTo( QPointF( zoomRect().left(), min ) );

    Q_EMIT zoomed( zoomRect() );
}

int ScrollZoomer::oppositeAxis( int axis ) const
{
    switch( axis )
    {
        case QwtPlot::xBottom:
            return QwtPlot::xTop;
        case QwtPlot::xTop:
            return QwtPlot::xBottom;
        case QwtPlot::yLeft:
            return QwtPlot::yRight;
        case QwtPlot::yRight:
            return QwtPlot::yLeft;
        default:
            break;
    }

    return axis;
}

#include <QDebug>

void ScrollZoomer::widgetMouseMoveEvent(QMouseEvent *event)
{
    auto point = invTransform(event->pos()); // translate from pixel to plot coordinates
    emit mouseCursorMovedTo(point);
    QwtPlotZoomer::widgetMouseMoveEvent(event);
}

void ScrollZoomer::widgetLeaveEvent( QEvent * event )
{
    emit mouseCursorLeftPlot();
    QwtPlotZoomer::widgetLeaveEvent(event);
}

void ScrollZoomer::widgetMouseReleaseEvent(QMouseEvent *me)
{
    if (m_ignoreNextMouseRelease)
    {
        m_ignoreNextMouseRelease = false;
    }
    else
    {
        QwtPlotZoomer::widgetMouseReleaseEvent(me);
    }
}

// Note: modified version of the implementation in qwt_plot_picker.cpp
QwtText ScrollZoomer::trackerTextF( const QPointF &pos ) const
{
    QString text;

    switch ( rubberBand() )
    {
        case HLineRubberBand:
            text = QString::number(pos.y(), 'g', 4);
            break;
        case VLineRubberBand:
            text = QString::number(pos.x(), 'g', 4);
            break;
        default:
            /*
            text.sprintf("%lld, %lld",
                         static_cast<int64_t>(m_conversionX.transform(pos.x())),
                         static_cast<int64_t>(pos.y()));
            */
            break;
    }

    return QwtText( text );
}

void ScrollZoomer::ignoreNextMouseRelease()
{
    m_ignoreNextMouseRelease = true;
}
