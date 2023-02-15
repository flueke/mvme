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
#include <qstyle.h>
#include <qstyleoption.h>
#include "scrollbar.h"

ScrollBar::ScrollBar( QWidget * parent ):
    QScrollBar( parent )
{
    init();
}

ScrollBar::ScrollBar( Qt::Orientation o,
        QWidget *parent ):
    QScrollBar( o, parent )
{
    init();
}

ScrollBar::ScrollBar( double minBase, double maxBase,
        Qt::Orientation o, QWidget *parent ):
    QScrollBar( o, parent )
{
    init();
    setBase( minBase, maxBase );
    moveSlider( minBase, maxBase );
}

void ScrollBar::init()
{
    d_inverted = orientation() == Qt::Vertical;
    d_baseTicks = 1000000;
    d_minBase = 0.0;
    d_maxBase = 1.0;
    moveSlider( d_minBase, d_maxBase );

    connect( this, SIGNAL( sliderMoved( int ) ), SLOT( catchSliderMoved( int ) ) );
    connect( this, SIGNAL( valueChanged( int ) ), SLOT( catchValueChanged( int ) ) );
}

void ScrollBar::setInverted( bool inverted )
{
    if ( d_inverted != inverted )
    {
        d_inverted = inverted;
        moveSlider( minSliderValue(), maxSliderValue() );
    }
}

bool ScrollBar::isInverted() const
{
    return d_inverted;
}

void ScrollBar::setBase( double min, double max )
{
    if ( min != d_minBase || max != d_maxBase )
    {
        d_minBase = min;
        d_maxBase = max;

        moveSlider( minSliderValue(), maxSliderValue() );
    }
}

void ScrollBar::moveSlider( double min, double max )
{
    const int sliderTicks = qRound( ( max - min ) /
        ( d_maxBase - d_minBase ) * d_baseTicks );

    // setRange initiates a valueChanged of the scrollbars
    // in some situations. So we block
    // and unblock the signals.

    blockSignals( true );

    setRange( sliderTicks / 2, d_baseTicks - sliderTicks / 2 );
    int steps = sliderTicks / 200;
    if ( steps <= 0 )
        steps = 1;

    setSingleStep( steps );
    setPageStep( sliderTicks );

    int tick = mapToTick( min + ( max - min ) / 2 );
    if ( isInverted() )
        tick = d_baseTicks - tick;

    setSliderPosition( tick );
    blockSignals( false );
}

double ScrollBar::minBaseValue() const
{
    return d_minBase;
}

double ScrollBar::maxBaseValue() const
{
    return d_maxBase;
}

void ScrollBar::sliderRange( int value, double &min, double &max ) const
{
    if ( isInverted() )
        value = d_baseTicks - value;

    const int visibleTicks = pageStep();

    min = mapFromTick( value - visibleTicks / 2 );
    max = mapFromTick( value + visibleTicks / 2 );
}

double ScrollBar::minSliderValue() const
{
    double min, dummy;
    sliderRange( value(), min, dummy );

    return min;
}

double ScrollBar::maxSliderValue() const
{
    double max, dummy;
    sliderRange( value(), dummy, max );

    return max;
}

int ScrollBar::mapToTick( double v ) const
{
    const double pos = ( v - d_minBase ) / ( d_maxBase - d_minBase ) * d_baseTicks;
    return static_cast<int>( pos );
}

double ScrollBar::mapFromTick( int tick ) const
{
    return d_minBase + ( d_maxBase - d_minBase ) * tick / d_baseTicks;
}

void ScrollBar::catchValueChanged( int value )
{
    double min, max;
    sliderRange( value, min, max );
    Q_EMIT valueChanged( orientation(), min, max );
}

void ScrollBar::catchSliderMoved( int value )
{
    double min, max;
    sliderRange( value, min, max );
    Q_EMIT sliderMoved( orientation(), min, max );
}

int ScrollBar::extent() const
{
    QStyleOptionSlider opt;
    opt.init( this );
    opt.subControls = QStyle::SC_None;
    opt.activeSubControls = QStyle::SC_None;
    opt.orientation = orientation();
    opt.minimum = minimum();
    opt.maximum = maximum();
    opt.sliderPosition = sliderPosition();
    opt.sliderValue = value();
    opt.singleStep = singleStep();
    opt.pageStep = pageStep();
    opt.upsideDown = invertedAppearance();
    if ( orientation() == Qt::Horizontal )
        opt.state |= QStyle::State_Horizontal;
    return style()->pixelMetric( QStyle::PM_ScrollBarExtent, &opt, this );
}
