#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_

#include <cmath>
#include <QGridLayout>
#include <qwt_plot.h>

#include "analysis/analysis.h"
#include "histo_ui.h"
#include "mvme_qwt.h"
#include "scrollzoomer.h"

 // Private header so these are fine
using namespace histo_ui;
using namespace mvme_qwt;

class TilePlot: public QwtPlot
{
    Q_OBJECT
    public:
        static const int DefaultMaxColumns = 4;
        static const int TileMinWidth = 150;
        static const int TileMinHeight = 150;
        static const int TileDeltaWidth = 50;
        static const int TileDeltaHeight = 50;

        explicit TilePlot(QWidget *parent = nullptr);
        ~TilePlot() override;

        QSize sizeHint() const override;

        void addToTileSize(const QSize &size)
        {
            auto tileSize = minimumSize();
            tileSize += size;
            if (tileSize.width() < TileMinWidth)
                tileSize.setWidth(TileMinWidth);
            if (tileSize.height() < TileMinHeight)
                tileSize.setHeight(TileMinHeight);
            setMinimumSize(tileSize);
        }

        // Which plot axis to use for the x axis. If the plot is in the top row
        // xTop will be used, otherwise xBottom.
        QwtPlot::Axis plotXAxis() const { return xAxis_; }
        void setPlotXAxis(QwtPlot::Axis axis) { xAxis_ = axis; }

        // Which plot axis to use for the y axis. If the plot is in the
        // rightmost column yRight will be used, otherwise yLeft.
        QwtPlot::Axis plotYAxis() const { return yAxis_; }
        void setPlotYAxis(QwtPlot::Axis axis) { yAxis_ = axis; }

    private:
        QwtPlot::Axis xAxis_ = QwtPlot::Axis::xBottom;
        QwtPlot::Axis yAxis_ = QwtPlot::Axis::yLeft;
};

/*****************************************************************************
 * Based on: Qwt Examples - Copyright (C) 2002 Uwe Rathmann
 * This file may be used under the terms of the 3-clause BSD License
 *****************************************************************************/
class PlotMatrix : public QFrame
{
    Q_OBJECT

  public:
    PlotMatrix( int rows, int columns, QWidget* parent = NULL );
    virtual ~PlotMatrix();

    int numRows() const;
    int numColumns() const;

    TilePlot* plotAt( int row, int column );
    const TilePlot* plotAt( int row, int column ) const;

    void setAxisVisible( int axisId, bool tf = true );
    bool isAxisVisible( int axisId ) const;

    void setAxisScale( int axisId, int rowOrColumn,
        double min, double max, double step = 0 );

    QGridLayout *plotGrid();

  protected:
    void updateLayout();

  private Q_SLOTS:
    void scaleDivChanged();

  private:
    void alignAxes( int rowOrColumn, int axis );
    void alignScaleBorder( int rowOrColumn, int axis );

    class PrivateData;
    PrivateData* m_data;
};

struct PlotEntry
{
    public:
        explicit PlotEntry(QWidget *plotParent, QwtPlot::Axis scaleAxis = QwtPlot::Axis::yLeft)
            : plot_(new TilePlot(plotParent))
            , zoomer_(new ScrollZoomer(plot_->canvas()))
            , scaleChanger_(new PlotAxisScaleChanger(plot_, scaleAxis))
        {
            zoomer_->setEnabled(false);
            zoomer_->setZoomBase();
        }

        explicit PlotEntry(TilePlot *tilePlot, QwtPlot::Axis scaleAxis = QwtPlot::Axis::yLeft)
            : plot_(tilePlot)
            , zoomer_(new ScrollZoomer(plot_->canvas()))
            , scaleChanger_(new PlotAxisScaleChanger(plot_, scaleAxis))
        {
            zoomer_->setEnabled(false);
            zoomer_->setZoomBase();
        }

        virtual ~PlotEntry() {}
        PlotEntry(const PlotEntry &) = delete;
        PlotEntry &operator=(const PlotEntry &) = delete;

        TilePlot *plot() { return plot_; }
        ScrollZoomer *zoomer() { return zoomer_; }
        PlotAxisScaleChanger *scaleChanger() { return scaleChanger_; }
        virtual void refresh() {};

    private:
        TilePlot *plot_;
        ScrollZoomer *zoomer_;
        PlotAxisScaleChanger *scaleChanger_;
};

struct Histo1DSinkPlotEntry: public PlotEntry
{
    using SinkPtr = std::shared_ptr<analysis::Histo1DSink>;

    Histo1DSinkPlotEntry(const SinkPtr &sink_, const Histo1DPtr &histo_, QWidget *plotParent)
        : PlotEntry(plotParent)
        , sink(sink_)
        , histo(histo_)
        , plotItem(new QwtPlotHistogram)
        , histoData(new Histo1DIntervalData(histo.get()))
        , gaussCurve(make_plot_curve(Qt::green))
        , gaussCurveData(new Histo1DGaussCurveData)
    {
        plotItem->setStyle(QwtPlotHistogram::Outline);
        plotItem->setData(histoData); // ownership of histoData goes to qwt
        plotItem->attach(plot());

        gaussCurve->setData(gaussCurveData);
        gaussCurve->hide();
        gaussCurve->attach(plot());

        zoomer()->setHScrollBarMode(Qt::ScrollBarAlwaysOff);
        zoomer()->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    }

    void refresh() override
    {
        auto histoStats = histoData->getHisto()->calcStatistics();
        gaussCurveData->setStats(histoStats);

        // x-axis update
        if (zoomer()->zoomRectIndex() == 0)
        {
            // fully zoomed out -> set to full resolution
            plot()->setAxisScale(plot()->plotXAxis(), histo->getXMin(), histo->getXMax());
            zoomer()->setZoomBase();
        }

        // do not zoom outside the histogram range
        auto scaleDiv = plot()->axisScaleDiv(plot()->plotXAxis());
        double lowerBound = scaleDiv.lowerBound();
        double upperBound = scaleDiv.upperBound();

        if (lowerBound <= upperBound)
        {
            if (lowerBound < histo->getXMin())
                scaleDiv.setLowerBound(histo->getXMin());

            if (upperBound > histo->getXMax())
                scaleDiv.setUpperBound(histo->getXMax());
        }
        else
        {
            if (lowerBound > histo->getXMin())
                scaleDiv.setLowerBound(histo->getXMin());

            if (upperBound < histo->getXMax())
                scaleDiv.setUpperBound(histo->getXMax());
        }

        plot()->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);

        // y-axis update
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

        // This sets a fixed y axis scale effectively overriding any changes made
        // by the scrollzoomer.
        plot()->setAxisScale(plot()->plotYAxis(), base, maxValue);
        plot()->updateAxes();

        zoomer()->setAxis(plot()->plotXAxis(), plot()->plotYAxis());
    }

    SinkPtr sink;
    Histo1DPtr histo; // to keep a copy of the histo alive
    QwtPlotHistogram *plotItem;
    Histo1DIntervalData *histoData;
    QwtPlotCurve *gaussCurve;
    Histo1DGaussCurveData *gaussCurveData;
};

struct Histo2DSinkPlotEntry: public PlotEntry
{
    using SinkPtr = std::shared_ptr<analysis::Histo2DSink>;
};

struct RateMonitorSinkPlotEntry: public PlotEntry
{
    using SinkPtr = std::shared_ptr<analysis::RateMonitorSink>;
};

void enable_plot_axis(QwtPlot* plot, int axis, bool on);

enum class GridScaleDrawMode
{
    ShowAll,
    HideInner,
};

inline std::pair<int, int> row_col_from_index(int index, int columns)
{
    int row = std::floor(index / columns);
    int col = index % columns;
    return std::pair(row, col);
}

void set_plot_axes(QGridLayout *grid, GridScaleDrawMode mode);

#endif // __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_