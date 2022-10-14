#include "multiplot_widget_p.h"

#include <qwt_plot_canvas.h>

TilePlot::TilePlot(QWidget *parent)
    : QwtPlot(parent)
{
    auto canvas = new QwtPlotCanvas;
    canvas->setLineWidth(1);
    canvas->setFrameStyle(QFrame::Box | QFrame::Plain);
    setCanvas(canvas);
}

TilePlot::~TilePlot() { }

QSize TilePlot::sizeHint() const
{
    return minimumSizeHint();
}