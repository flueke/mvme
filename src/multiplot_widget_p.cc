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

void set_plot_axes(QGridLayout *grid, GridScaleDrawMode mode)
{
    for (int index = 0; index < grid->count(); ++index)
    {
        if (auto li = grid->itemAt(index);
            auto plot = qobject_cast<TilePlot *>(li->widget()))
        {
            auto [row, col] = row_col_from_index(index, grid->columnCount());

            switch (mode)
            {
                case GridScaleDrawMode::ShowAll:
                {
                    plot->setPlotXAxis(QwtPlot::xBottom);
                    plot->setPlotYAxis(QwtPlot::yLeft);
                    enable_plot_axis(plot, QwtPlot::xBottom, true);
                    enable_plot_axis(plot, QwtPlot::xTop, false);
                    enable_plot_axis(plot, QwtPlot::yLeft, true);
                    enable_plot_axis(plot, QwtPlot::yRight, false);
                } break;

                case GridScaleDrawMode::HideInner:
                {
                    if (row == 0)
                    {
                        plot->setPlotXAxis(QwtPlot::xTop);
                        enable_plot_axis(plot, QwtPlot::xTop, true);
                        enable_plot_axis(plot, QwtPlot::xBottom, false);
                    }
                    else if (row == grid->rowCount() - 1 || !grid->itemAtPosition(row+1, col))
                    {
                        plot->setPlotXAxis(QwtPlot::xBottom);
                        enable_plot_axis(plot, QwtPlot::xTop, false);
                        enable_plot_axis(plot, QwtPlot::xBottom, true);
                    }
                    else
                    {
                        plot->setPlotXAxis(QwtPlot::xBottom);
                        enable_plot_axis(plot, QwtPlot::xTop, false);
                        enable_plot_axis(plot, QwtPlot::xBottom, false);
                    }

                    if (col == 0)
                    {
                        plot->setPlotYAxis(QwtPlot::yLeft);
                        enable_plot_axis(plot, QwtPlot::yLeft, true);
                        enable_plot_axis(plot, QwtPlot::yRight, false);
                    }
                    else if (col == grid->columnCount() - 1)
                    {
                        plot->setPlotYAxis(QwtPlot::yRight);
                        enable_plot_axis(plot, QwtPlot::yLeft, false);
                        enable_plot_axis(plot, QwtPlot::yRight, true);
                    }
                    else
                    {
                        plot->setPlotYAxis(QwtPlot::yLeft);
                        enable_plot_axis(plot, QwtPlot::yLeft, false);
                        enable_plot_axis(plot, QwtPlot::yRight, false);
                    }

                }
            };
        }
    }
}