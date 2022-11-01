#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_

#include <cmath>
#include <QGridLayout>
#include <qwt_plot.h>
#include <qwt_plot_spectrogram.h>

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

        // Which plot axis to use for the x title.
        QwtPlot::Axis xTitleAxis() const { return xTitleAxis_; }
        void setXTitleAxis(QwtPlot::Axis axis) { xTitleAxis_ = axis; }

        // Which plot axis to use for the y title.
        QwtPlot::Axis yTitleAxis() const { return yTitleAxis_; }
        void setYTitleAxis(QwtPlot::Axis axis) { yTitleAxis_ = axis; }

    private:
        QwtPlot::Axis xAxis_ = QwtPlot::Axis::xBottom;
        QwtPlot::Axis yAxis_ = QwtPlot::Axis::yLeft;

        QwtPlot::Axis xTitleAxis_ = QwtPlot::Axis::xTop;
        QwtPlot::Axis yTitleAxis_ = QwtPlot::Axis::yLeft;
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
            resReductions_.fill(0);
        }

        explicit PlotEntry(TilePlot *tilePlot, QwtPlot::Axis scaleAxis = QwtPlot::Axis::yLeft)
            : plot_(tilePlot)
            , zoomer_(new ScrollZoomer(plot_->canvas()))
            , scaleChanger_(new PlotAxisScaleChanger(plot_, scaleAxis))
        {
            zoomer_->setEnabled(false);
            zoomer_->setZoomBase();
            resReductions_.fill(0);
        }

        virtual ~PlotEntry() {}
        PlotEntry(const PlotEntry &) = delete;
        PlotEntry &operator=(const PlotEntry &) = delete;

        TilePlot *plot() { return plot_; }
        ScrollZoomer *zoomer() { return zoomer_; }
        PlotAxisScaleChanger *scaleChanger() { return scaleChanger_; }
        virtual void refresh() {};

        // resolution reduction factor
        u32 rrf(Qt::Axis axis)
        {
            if (axis < resReductions_.size())
                return resReductions_[axis];
            return 0;
        }

        void setRRF(Qt::Axis axis, u32 rrf)
        {
            if (axis < resReductions_.size())
                resReductions_[axis] = rrf;
        }

    protected:
        int xMajorTicks = 5;
        int yMajorTicks = 5;

    private:
        TilePlot *plot_;
        ScrollZoomer *zoomer_;
        PlotAxisScaleChanger *scaleChanger_;
        std::array<u32, 3> resReductions_;
};

struct Histo1DSinkPlotEntry: public PlotEntry
{
    using SinkPtr = std::shared_ptr<analysis::Histo1DSink>;

    Histo1DSinkPlotEntry(const SinkPtr &sink_, const Histo1DPtr &histo_, QWidget *plotParent)
        : PlotEntry(plotParent)
        , sink(sink_)
        , histo(histo_)
        , histoIndex(sink->getHistos().indexOf(histo))
        , plotItem(new QwtPlotHistogram)
        , histoData(new Histo1DIntervalData(histo.get()))
        , gaussCurve(make_plot_curve(Qt::green))
        , gaussCurveData(new Histo1DGaussCurveData)
        , statsTextItem(new TextLabelItem)
    {
        histoIndex = sink->getHistos().indexOf(histo_);
        assert(histoIndex >= 0); // the histo should be one of the sinks histograms

        plotItem->setStyle(QwtPlotHistogram::Outline);
        plotItem->setData(histoData); // ownership of histoData goes to qwt
        plotItem->attach(plot());

        gaussCurve->setData(gaussCurveData);
        gaussCurve->hide();
        gaussCurve->attach(plot());

        statsTextItem->hide();
        statsTextItem->attach(plot());

        zoomer()->setHScrollBarMode(Qt::ScrollBarAlwaysOff);
        zoomer()->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    }

    void refresh() override;

    SinkPtr sink;
    Histo1DPtr histo; // to keep a copy of the histo alive
    int histoIndex; // index of this entries histogram in the sink
    QwtPlotHistogram *plotItem;
    Histo1DIntervalData *histoData;
    QwtPlotCurve *gaussCurve;
    Histo1DGaussCurveData *gaussCurveData;
    TextLabelItem *statsTextItem;
};

struct Histo2DSinkPlotEntry: public PlotEntry
{
    using SinkPtr = std::shared_ptr<analysis::Histo2DSink>;

    Histo2DSinkPlotEntry(const SinkPtr &sink_, QWidget *plotParent)
        : PlotEntry(plotParent, QwtPlot::yRight)
        , sink(sink_)
        , histo(sink->getHisto())
        , plotItem(new QwtPlotSpectrogram)
        , histoData(new Histo2DRasterData(histo.get()))
        , statsTextItem(new TextLabelItem)
    {
        plotItem->setRenderThreadCount(0);
        plotItem->setData(histoData);
        plotItem->attach(plot());

        statsTextItem->hide();
        statsTextItem->attach(plot());

        zoomer()->setHScrollBarMode(Qt::ScrollBarAlwaysOff);
        zoomer()->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    }

    void refresh() override;

    SinkPtr sink;
    Histo2DPtr histo; // to ensure the histo stays alive
    QwtPlotSpectrogram *plotItem;
    Histo2DRasterData *histoData;
    TextLabelItem *statsTextItem;
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

void set_plot_grid_scale_draw_mode(
    QGridLayout *plotGrid, GridScaleDrawMode xScaleMode, GridScaleDrawMode yScaleMode);

//void set_plot_axes(QGridLayout *grid, GridScaleDrawMode mode);

/*****************************************************************************
 * Based on: Qwt Examples - Copyright (C) 2002 Uwe Rathmann
 * This file may be used under the terms of the 3-clause BSD License
 *****************************************************************************/
class PlotMatrix : public QFrame
{
    Q_OBJECT

  public:
    PlotMatrix( int rows, int columns, QWidget* parent = NULL );
    PlotMatrix(const std::vector<std::shared_ptr<PlotEntry>> entries, int maxColumns, QWidget *parent = nullptr);
    virtual ~PlotMatrix();

    int numRows() const;
    int numColumns() const;

    TilePlot* plotAt( int row, int column );
    const TilePlot* plotAt( int row, int column ) const;

    void setAxisVisible( int axisId, bool tf = true );
    bool isAxisVisible( int axisId ) const;

    //void setAxisScale( int axisId, int rowOrColumn,
    //    double min, double max, double step = 0 );

    QGridLayout *plotGrid();

  protected:
    void updateLayout();

  //private Q_SLOTS:
  //  void onScaleDivChanged();

  private:
    void alignAxes( int rowOrColumn, int axis );
    void alignScaleBorder( int rowOrColumn, int axis );

    class PrivateData;
    PrivateData* m_data;
};


#endif // __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_P_H_