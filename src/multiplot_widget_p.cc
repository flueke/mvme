#include "multiplot_widget_p.h"

#include <qwt_plot_canvas.h>
#include <qwt_plot.h>
#include <qwt_scale_draw.h>
#include <qwt_scale_widget.h>

TilePlot::TilePlot(QWidget *parent)
    : QwtPlot(parent)
{
    auto canvas = new QwtPlotCanvas;
    canvas->setLineWidth(1);
    canvas->setFrameStyle(QFrame::Box | QFrame::Plain);
    setCanvas(canvas);
    canvas->unsetCursor();
    setMinimumSize(TileMinWidth, TileMinHeight);
    //setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
}

TilePlot::~TilePlot() {}

QSize TilePlot::sizeHint() const
{
    return minimumSizeHint();
}

void enable_plot_axis(QwtPlot* plot, int axis, bool on)
{
    // when false we still enable the axis to have an effect
    // of the minimal extent active. Instead we hide all visible
    // parts and margins/spacings.

    plot->enableAxis(axis, true);

    QwtScaleDraw* sd = plot->axisScaleDraw( axis );
    sd->enableComponent( QwtScaleDraw::Backbone, on );
    sd->enableComponent( QwtScaleDraw::Ticks, on );
    sd->enableComponent( QwtScaleDraw::Labels, on );

    QwtScaleWidget* sw = plot->axisWidget( axis );
    sw->setMargin( on ? 4 : 0 );
    sw->setSpacing( on ? 20 : 0 );
}