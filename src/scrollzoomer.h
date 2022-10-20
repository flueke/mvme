/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef _SCROLLZOOMER_H
#define _SCROLLZOOMER_H

#include <qglobal.h>
#include <qwt_plot_zoomer.h>
#include <qwt_plot.h>
#include <qwt_scale_map.h>

#include "libmvme_export.h"

class ScrollData;
class ScrollBar;

class LIBMVME_EXPORT ScrollZoomer: public QwtPlotZoomer
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

    explicit ScrollZoomer( QWidget * );
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

    virtual bool eventFilter( QObject *, QEvent * ) override;

    virtual void rescale() override;
    quint32 getLowborder();
    quint32 getHiborder();

    void ignoreNextMouseRelease();

    #if QWT_VERSION >= 0x060200
    void setAxis(int xAxis, int yAxis)
    {
        setAxes(xAxis, yAxis);
    }
    #endif

protected:
    virtual ScrollBar *scrollBar( Qt::Orientation );
    virtual void updateScrollBars();
    virtual void layoutScrollBars( const QRect & );

    virtual void widgetMouseMoveEvent(QMouseEvent *event) override;
    virtual void widgetLeaveEvent( QEvent * ) override;
    virtual void widgetMouseReleaseEvent(QMouseEvent *me) override;

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
    bool m_ignoreNextMouseRelease = false;
};

#endif
