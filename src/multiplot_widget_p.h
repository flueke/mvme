#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_

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

        // Which plot axis to use for the y axis. If the plot is in the
        // rightmost column yRight will be used, otherwise yLeft.
        QwtPlot::Axis plotYAxis() const { return yAxis_; }
        void setPlotYAxis(QwtPlot::Axis axis) { yAxis_ = axis; }

    private:
        QwtPlot::Axis yAxis_ = QwtPlot::Axis::yLeft;
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
            plot()->setAxisScale(QwtPlot::xBottom, histo->getXMin(), histo->getXMax());
            zoomer()->setZoomBase();
        }

        // do not zoom outside the histogram range
        auto scaleDiv = plot()->axisScaleDiv(QwtPlot::xBottom);
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

#endif // __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_