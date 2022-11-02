#include "multiplot_widget_p.h"

#include <qwt_plot_canvas.h>
#include <qwt_plot.h>
#include <qwt_scale_draw.h>
#include <qwt_scale_widget.h>

#include "histo_gui_util.h"

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

TilePlot::~TilePlot()
{
    //qDebug() << __PRETTY_FUNCTION__ << this;
}

QSize TilePlot::sizeHint() const
{
    return minimumSizeHint();
}

void Histo1DSinkPlotEntry::refresh()
{
    histoData->setResolutionReductionFactor(rrf(Qt::XAxis));

    // x-axis update
    // ====================
    if (zoomer()->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        plot()->setAxisScale(plot()->plotXAxis(), histo->getXMin(), histo->getXMax());
        zoomer()->setZoomBase();
    }

    // do not zoom outside the histogram range
    auto xScaleDiv = plot()->axisScaleDiv(plot()->plotXAxis());
    double lowerBound = xScaleDiv.lowerBound();
    double upperBound = xScaleDiv.upperBound();

    if (lowerBound <= upperBound)
    {
        if (lowerBound < histo->getXMin())
            xScaleDiv.setLowerBound(histo->getXMin());

        if (upperBound > histo->getXMax())
            xScaleDiv.setUpperBound(histo->getXMax());
    }
    else
    {
        if (lowerBound > histo->getXMin())
            xScaleDiv.setLowerBound(histo->getXMin());

        if (upperBound < histo->getXMax())
            xScaleDiv.setUpperBound(histo->getXMax());
    }

    if (auto scaleEngine = plot()->axisScaleEngine(plot()->plotXAxis()))
    {
        // max of N major ticks and no minor ticks
        xScaleDiv = scaleEngine->divideScale(xScaleDiv.lowerBound(), xScaleDiv.upperBound(), xMajorTicks, 0);
        plot()->setAxisScaleDiv(plot()->plotXAxis(), xScaleDiv);
    }

    // Calculate histo statistics over the visible x range
    auto histoStats = histo->calcStatistics(
        xScaleDiv.lowerBound(), xScaleDiv.upperBound(), rrf(Qt::XAxis));

    // Update the gauss curve with the calculated histo stats.
    gaussCurveData->setStats(histoStats);

    // y-axis update
    // ====================

    // Scale the y axis using the currently visible max value plus 20%
    double maxValue = histoStats.maxValue;

    // force a minimum of 10 units in y
    if (maxValue <= 1.0)
        maxValue = 10.0;

    double base = 0.0;

    if (is_logarithmic_axis_scale(plot(), plot()->plotYAxis()))
    {
        base = 1.0;
        maxValue = std::pow(maxValue, 1.2);
    }
    else
    {
        base = 0.0;
        maxValue = maxValue * 1.2;
    }

    // This sets a fixed y axis scale effectively overriding any changes
    // made by the scrollzoomer.
    plot()->setAxisScale(plot()->plotYAxis(), base, maxValue);

    if (auto scaleEngine = plot()->axisScaleEngine(plot()->plotYAxis()))
    {
        // max of N major ticks and no minor ticks
        auto yScaleDiv = scaleEngine->divideScale(base, maxValue, yMajorTicks, 0);
        plot()->setAxisScaleDiv(plot()->plotYAxis(), yScaleDiv);
    }

    // Set the plot title
    {
        QwtText title(sink->objectName());
        auto font = title.font();
        font.setPointSize(10);
        title.setFont(font);
        plot()->setTitle(title);
    }

    // x axis title
    #if 0
    {
        auto axisInfo = histo->getAxisInfo(Qt::XAxis);
        QwtText title(make_title_string(axisInfo));
        auto font = title.font();
        font.setPointSize(10);
        title.setFont(font);
        plot()->setAxisTitle(plot()->xTitleAxis(), title);
    }
    #endif

    // Update the stats text box
    // =========================
    static const QString RowTemplate = "<tr><td align=\"left\">%1</td><td>%2</td></tr>";
    QStringList textRows;

    textRows << "<table>";
    textRows << RowTemplate.arg("Counts").arg(histoStats.entryCount);

    if (gaussCurve->isVisible())
        textRows << RowTemplate.arg("FWHM").arg(histoStats.fwhm, 0, 'g', 4);

    textRows << "</table>";

    auto statsQwtText = make_qwt_text_box(Qt::AlignTop | Qt::AlignRight, 8);
    statsQwtText->setText(textRows.join("\n"));
    statsTextItem->setText(*statsQwtText);
    statsTextItem->show();

    // Final plot and zoomer axes update
    // ====================
    plot()->updateAxes(); // let qwt recalculate the axes
    zoomer()->setAxis(plot()->plotXAxis(), plot()->plotYAxis());
}

std::unique_ptr<QwtColorMap> Histo2DSinkPlotEntry::makeColorMap()
{
    if (is_logarithmic_axis_scale(plot(), QwtPlot::yRight))
        return make_histo2d_color_map(AxisScaleType::Logarithmic);
    return make_histo2d_color_map(AxisScaleType::Linear);
}

void Histo2DSinkPlotEntry::refresh()
{
    histoData->setResolutionReductionFactors(rrf(Qt::XAxis), rrf(Qt::YAxis));

    auto histo = sink->getHisto();

    // axis and stats updates
    // ====================
    if (zoomer()->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        plot()->setAxisScale(plot()->plotXAxis(), histo->getXMin(), histo->getXMax());
        plot()->setAxisScale(plot()->plotYAxis(), histo->getYMin(), histo->getYMax());
        zoomer()->setZoomBase();
    }

    auto xInterval = plot()->axisScaleDiv(QwtPlot::xBottom).interval();
    auto yInterval = plot()->axisScaleDiv(QwtPlot::yLeft).interval();

    auto histoStats = histo->calcStatistics(
        { xInterval.minValue(), xInterval.maxValue() },
        { yInterval.minValue(), yInterval.maxValue() },
        { rrf(Qt::XAxis), rrf(Qt::YAxis) }
        );

    QwtInterval zInterval
    {
        histoStats.intervals[Qt::ZAxis].minValue,
        histoStats.intervals[Qt::ZAxis].maxValue
    };

    double zBase = is_logarithmic_axis_scale(plot(), QwtPlot::yRight) ? 1.0 : 0.0;

    zInterval.setMinValue(zBase);

    if (zInterval.width() <= 0.0)
        zInterval.setMaxValue(is_logarithmic_axis_scale(plot(), QwtPlot::yRight) ? 2.0 : 1.0);

    assert(zInterval.width() > 0.0);

    plot()->setAxisScale(QwtPlot::yRight, zInterval.minValue(), zInterval.maxValue());
    plot()->axisWidget(QwtPlot::yRight)->setColorMap(zInterval, makeColorMap().release());
    plotItem->setColorMap(makeColorMap().release());

    // Set the intervals on the interal raster data object.
    histoData->setInterval(Qt::XAxis, xInterval);
    histoData->setInterval(Qt::YAxis, yInterval);
    histoData->setInterval(Qt::ZAxis, zInterval);

    // Set the plot title
    {
        QwtText title(sink->objectName());
        auto font = title.font();
        font.setPointSize(10);
        title.setFont(font);
        plot()->setTitle(title);
    }

    // x axis title
    {
        auto axisInfo = histo->getAxisInfo(Qt::XAxis);
        QwtText title(make_title_string(axisInfo));
        auto font = title.font();
        font.setPointSize(10);
        title.setFont(font);
        plot()->setAxisTitle(plot()->xTitleAxis(), title);

    }

    // y axis title
    {
        auto axisInfo = histo->getAxisInfo(Qt::YAxis);
        QwtText title(make_title_string(axisInfo));
        auto font = title.font();
        font.setPointSize(10);
        title.setFont(font);
        plot()->setAxisTitle(plot()->yTitleAxis(), title);
    }

    // Update the stats text box
    // =========================
    static const QString RowTemplate = "<tr><td align=\"left\">%1</td><td>%2</td></tr>";
    QStringList textRows;

    textRows << "<table>";
    textRows << RowTemplate.arg("Counts").arg(histoStats.entryCount);
    textRows << "</table>";

    auto statsQwtText = make_qwt_text_box(Qt::AlignTop | Qt::AlignRight, 8);
    statsQwtText->setText(textRows.join("\n"));
    statsTextItem->setText(*statsQwtText);
    statsTextItem->show();

    // Final plot and zoomer axes update
    // ====================
    plot()->updateAxes(); // let qwt recalculate the axes
    zoomer()->setAxis(plot()->plotXAxis(), plot()->plotYAxis());
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

    //QwtScaleWidget* sw = plot->axisWidget( axis );
    //sw->setMargin( on ? 4 : 0 );
    //sw->setSpacing( on ? 20 : 0 );
}

class PlotMatrix::PrivateData
{
  public:
    PrivateData():
        inScaleSync( false )
    {
        isAxisEnabled[QwtPlot::xBottom] = true;
        isAxisEnabled[QwtPlot::xTop] = false;
        isAxisEnabled[QwtPlot::yLeft] = true;
        isAxisEnabled[QwtPlot::yRight] = false;
    }

    bool isAxisEnabled[QwtPlot::axisCnt];
    QVector< TilePlot * > plotWidgets;
    mutable bool inScaleSync;
};

PlotMatrix::PlotMatrix( int numRows, int numColumns, QWidget* parent )
    : QFrame( parent )
{
    m_data = new PrivateData();
    m_data->plotWidgets.resize( numRows * numColumns );

    QGridLayout* layout = new QGridLayout( this );
    layout->setSpacing( 5 );

    for ( int row = 0; row < numRows; row++ )
    {
        for ( int col = 0; col < numColumns; col++ )
        {
            auto plot = new TilePlot( this );

            layout->addWidget( plot, row, col );

            for ( int axisPos = 0; axisPos < QwtPlot::axisCnt; axisPos++ )
            {
                #if 0
                connect( plot->axisWidget( axisPos ),
                    SIGNAL(scaleDivChanged()), SLOT(onScaleDivChanged()),
                    Qt::QueuedConnection );
                #endif
            }
            m_data->plotWidgets[row * numColumns + col] = plot;
        }
    }

    updateLayout();
}

PlotMatrix::PlotMatrix(const std::vector<std::shared_ptr<PlotEntry>> entries, int maxColumns, QWidget *parent)
    : QFrame(parent)
    , m_data(new PrivateData)
{
    m_data->plotWidgets.resize(entries.size());

    auto layout = new QGridLayout(this);
    layout->setSpacing( 5 );

    for (auto &e: entries)
    {
        int index = layout->count();
        auto [row, col] = row_col_from_index(index, maxColumns);
        e->plot()->resize(100, 100);
        layout->addWidget(e->plot(), row, col);

        for ( int axisPos = 0; axisPos < QwtPlot::axisCnt; axisPos++ )
        {
            #if 0
            connect( e->plot()->axisWidget( axisPos ),
                SIGNAL(scaleDivChanged()), SLOT(onScaleDivChanged()),
                Qt::QueuedConnection );
            #endif
        }
        m_data->plotWidgets[row * maxColumns + col] = e->plot();
    }

    updateLayout();
}

PlotMatrix::~PlotMatrix()
{
    delete m_data;
}

int PlotMatrix::numRows() const
{
    const QGridLayout* l = qobject_cast< const QGridLayout* >( layout() );
    if ( l )
        return l->rowCount();

    return 0;
}

int PlotMatrix::numColumns() const
{
    const QGridLayout* l = qobject_cast< const QGridLayout* >( layout() );
    if ( l )
        return l->columnCount();
    return 0;
}

TilePlot* PlotMatrix::plotAt( int row, int column )
{
    const int index = row * numColumns() + column;
    if ( index < m_data->plotWidgets.size() )
        return m_data->plotWidgets[index];

    return NULL;
}

const TilePlot* PlotMatrix::plotAt( int row, int column ) const
{
    const int index = row * numColumns() + column;
    if ( index < m_data->plotWidgets.size() )
        return m_data->plotWidgets[index];

    return NULL;
}

void PlotMatrix::setAxisVisible( int axis, bool tf )
{
    if ( is_valid_axis( axis ) )
    {
        if ( tf != m_data->isAxisEnabled[axis] )
        {
            m_data->isAxisEnabled[axis] = tf;
            updateLayout();
        }
    }
}

bool PlotMatrix::isAxisVisible( int axis ) const
{
    if ( is_valid_axis( axis ) )
        return m_data->isAxisEnabled[axis];

    return false;
}

#if 0
void PlotMatrix::setAxisScale( int axis, int rowOrColumn,
    double min, double max, double step )
{
    int row = 0;
    int col = 0;

    if ( is_x_axis( axis ) )
        col = rowOrColumn;
    else
        row = rowOrColumn;

    QwtPlot* plt = plotAt( row, col );
    if ( plt )
    {
        plt->setAxisScale( axis, min, max, step );
        plt->updateAxes();
    }
}
#endif

QGridLayout *PlotMatrix::plotGrid()
{
    return qobject_cast<QGridLayout *>(layout());
}

#if 0
void PlotMatrix::onScaleDivChanged()
{
    if ( m_data->inScaleSync )
        return;

    qDebug() << __PRETTY_FUNCTION__;

    m_data->inScaleSync = true;

    QwtPlot* plt = NULL;
    int axisId = -1;
    int rowOrColumn = -1;

    // find the changed axis
    for ( int row = 0; row < numRows(); row++ )
    {
        for ( int col = 0; col < numColumns(); col++ )
        {
            QwtPlot* p = plotAt( row, col );
            if ( p )
            {
                for ( int axisPos = 0; axisPos < QwtPlot::axisCnt; axisPos++ )
                {
                    if ( p->axisWidget( axisPos ) == sender() )
                    {
                        plt = p;
                        axisId = axisPos;
                        rowOrColumn = is_x_axis( axisId ) ? col : row;
                    }
                }
            }
        }
    }

    if ( plt )
    {
        const QwtScaleDiv scaleDiv = plt->axisScaleDiv( axisId );

        // synchronize the axes
        if ( is_x_axis( axisId ) )
        {
            for ( int row = 0; row < numRows(); row++ )
            {
                QwtPlot* p = plotAt( row, rowOrColumn );
                if ( p != plt )
                {
                    p->setAxisScaleDiv( axisId, scaleDiv );
                }
            }
        }
        else
        {
            for ( int col = 0; col < numColumns(); col++ )
            {
                QwtPlot* p = plotAt( rowOrColumn, col );
                if ( p != plt )
                {
                    p->setAxisScaleDiv( axisId, scaleDiv );
                }
            }
        }

        updateLayout();
    }

    m_data->inScaleSync = false;
}
#endif

void PlotMatrix::updateLayout()
{
    for ( int row = 0; row < numRows(); row++ )
    {
        for ( int col = 0; col < numColumns(); col++ )
        {
            QwtPlot* p = plotAt( row, col );
            if ( p )
            {
                bool showAxis[QwtPlot::axisCnt];

                //showAxis[QwtPlot::xBottom] = isAxisVisible( QwtPlot::xBottom ) && row == numRows() - 1;
                //showAxis[QwtPlot::xTop] = isAxisVisible( QwtPlot::xTop ) && row == 0;
                //showAxis[QwtPlot::yLeft] = isAxisVisible( QwtPlot::yLeft ) && col == 0;
                //showAxis[QwtPlot::yRight] = isAxisVisible( QwtPlot::yRight ) && col == numColumns() - 1;
                showAxis[QwtPlot::xBottom] = true;
                showAxis[QwtPlot::xTop] = false;
                showAxis[QwtPlot::yLeft] = true;
                showAxis[QwtPlot::yRight] = false;

                for ( int axis = 0; axis < QwtPlot::axisCnt; axis++ )
                {
                    enable_plot_axis(p, axis, showAxis[axis]);
                }
            }
        }
    }

    for (int row = 0; row < numRows(); ++row)
        plotGrid()->setRowStretch(row, 1);

    for (int col = 0; col < numColumns(); ++col)
        plotGrid()->setColumnStretch(col, 1);


    for ( int row = 0; row < numRows(); row++ )
    {
        alignAxes( row, QwtPlot::xTop );
        alignAxes( row, QwtPlot::xBottom );

        alignScaleBorder( row, QwtPlot::yLeft );
        alignScaleBorder( row, QwtPlot::yRight );
    }

    for ( int col = 0; col < numColumns(); col++ )
    {
        alignAxes( col, QwtPlot::yLeft );
        alignAxes( col, QwtPlot::yRight );

        alignScaleBorder( col, QwtPlot::xBottom );
        alignScaleBorder( col, QwtPlot::xTop );
    }

    for ( int row = 0; row < numRows(); row++ )
    {
        for ( int col = 0; col < numColumns(); col++ )
        {
            QwtPlot* p = plotAt( row, col );
            if ( p )
                p->replot();
        }
    }
}

void PlotMatrix::alignAxes( int rowOrColumn, int axis )
{
    if ( is_y_axis(axis) )
    {
        double maxExtent = 0;

        for ( int row = 0; row < numRows(); row++ )
        {
            QwtPlot* p = plotAt( row, rowOrColumn );
            if ( p )
            {
                QwtScaleWidget* scaleWidget = p->axisWidget( axis );

                QwtScaleDraw* sd = scaleWidget->scaleDraw();
                sd->setMinimumExtent( 0.0 );

                const double extent = sd->extent( scaleWidget->font() );
                if ( extent > maxExtent )
                    maxExtent = extent;
            }
        }

        for ( int row = 0; row < numRows(); row++ )
        {
            QwtPlot* p = plotAt( row, rowOrColumn );
            if ( p )
            {
                QwtScaleWidget* scaleWidget = p->axisWidget( axis );
                scaleWidget->scaleDraw()->setMinimumExtent( maxExtent );
            }
        }
    }
    else
    {
        double maxExtent = 0;

        for ( int col = 0; col < numColumns(); col++ )
        {
            QwtPlot* p = plotAt( rowOrColumn, col );
            if ( p )
            {
                QwtScaleWidget* scaleWidget = p->axisWidget( axis );

                QwtScaleDraw* sd = scaleWidget->scaleDraw();
                sd->setMinimumExtent( 0.0 );

                const double extent = sd->extent( scaleWidget->font() );
                if ( extent > maxExtent )
                    maxExtent = extent;
            }
        }

        for ( int col = 0; col < numColumns(); col++ )
        {
            QwtPlot* p = plotAt( rowOrColumn, col );
            if ( p )
            {
                QwtScaleWidget* scaleWidget = p->axisWidget( axis );
                scaleWidget->scaleDraw()->setMinimumExtent( maxExtent );
            }
        }
    }
}

void PlotMatrix::alignScaleBorder( int rowOrColumn, int axis )
{
    int startDist = 0;
    int endDist = 0;

    if ( axis == QwtPlot::yLeft )
    {
        QwtPlot* plot = plotAt( rowOrColumn, 0 );
        if ( plot )
            plot->axisWidget( axis )->getBorderDistHint( startDist, endDist );

        for ( int col = 1; col < numColumns(); col++ )
        {
            plot = plotAt( rowOrColumn, col );
            if ( plot )
                plot->axisWidget( axis )->setMinBorderDist( startDist, endDist );
        }
    }
    else if ( axis == QwtPlot::yRight )
    {
        QwtPlot* plot = plotAt( rowOrColumn, numColumns() - 1 );
        if ( plot )
            plot->axisWidget( axis )->getBorderDistHint( startDist, endDist );

        for ( int col = 0; col < numColumns() - 1; col++ )
        {
            plot = plotAt( rowOrColumn, col );
            if ( plot )
                plot->axisWidget( axis )->setMinBorderDist( startDist, endDist );
        }
    }

    if ( axis == QwtPlot::xTop )
    {
        QwtPlot* plot = plotAt( rowOrColumn, 0 );
        if ( plot )
            plot->axisWidget( axis )->getBorderDistHint( startDist, endDist );

        for ( int row = 1; row < numRows(); row++ )
        {
            plot = plotAt( row, rowOrColumn );
            if ( plot )
                plot->axisWidget( axis )->setMinBorderDist( startDist, endDist );
        }
    }
    else if ( axis == QwtPlot::xBottom )
    {
        QwtPlot* plot = plotAt( numRows() - 1, rowOrColumn );
        if ( plot )
            plot->axisWidget( axis )->getBorderDistHint( startDist, endDist );

        for ( int row = 0; row < numRows() - 1; row++ )
        {
            plot = plotAt( row, rowOrColumn );
            if ( plot )
                plot->axisWidget( axis )->setMinBorderDist( startDist, endDist );
        }
    }
}