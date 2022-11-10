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

void enable_plot_axis(QwtPlot* plot, int axis, bool on);

class TilePlot: public QwtPlot
{
    Q_OBJECT
    public:
        static const int DefaultMaxColumns = 4;
        static const int TileMinWidth = 200;
        static const int TileMinHeight = TileMinWidth;
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

        // Which plot axis to use for the x axis.
        QwtPlot::Axis plotXAxis() const { return xAxis_; }
        void setPlotXAxis(QwtPlot::Axis axis) { xAxis_ = axis; }

        // Which plot axis to use for the y axis.
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

        QwtPlot::Axis xTitleAxis_ = QwtPlot::Axis::xBottom;
        QwtPlot::Axis yTitleAxis_ = QwtPlot::Axis::yLeft;
};

struct PlotEntry
{
    public:
        explicit PlotEntry(QWidget *plotParent, QwtPlot::Axis scaleAxis = QwtPlot::Axis::yLeft);
        explicit PlotEntry(TilePlot *tilePlot, QwtPlot::Axis scaleAxis = QwtPlot::Axis::yLeft);
        virtual ~PlotEntry() {}

        PlotEntry(const PlotEntry &) = delete;
        PlotEntry &operator=(const PlotEntry &) = delete;

        TilePlot *plot() { return plot_; }
        ScrollZoomer *zoomer() { return zoomer_; }
        PlotAxisScaleChanger *scaleChanger() { return scaleChanger_; }
        virtual void refresh() = 0;

        // resolution reduction factor
        inline u32 rrf(Qt::Axis axis)
        {
            return (axis < resReductions_.size() ?  resReductions_[axis] : 0u);
        }

        void setRRF(Qt::Axis axis, u32 rrf)
        {
            if (axis < resReductions_.size())
                resReductions_[axis] = rrf;
        }

        // May return 0 in case binCount() is not a concept for the entry or the
        // axis is unused. Otherwise should return the number of bins for the
        // axis without any resolution reduction applied (e.g. the number of
        // physical bins).
        virtual u32 binCount(Qt::Axis axis) const = 0;
        virtual analysis::AnalysisObjectPtr analysisObject() const = 0;

    protected:
        const int xMajorTicks = 5;
        const int yMajorTicks = 5;
        const int zMajorTicks = 5;

    private:
        TilePlot *plot_;
        ScrollZoomer *zoomer_;
        PlotAxisScaleChanger *scaleChanger_;
        std::array<u32, 3> resReductions_; // x, y, z resolution reduction factors
};

struct Histo1DSinkPlotEntry: public PlotEntry
{
    using SinkPtr = std::shared_ptr<analysis::Histo1DSink>;

    Histo1DSinkPlotEntry(const SinkPtr &sink_, const Histo1DPtr &histo_, QWidget *plotParent);
    void refresh() override;
    u32 binCount(Qt::Axis axis) const override;
    analysis::AnalysisObjectPtr analysisObject() const override { return sink; }

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

    Histo2DSinkPlotEntry(const SinkPtr &sink_, QWidget *plotParent);
    void refresh() override;
    u32 binCount(Qt::Axis axis) const override;
    std::unique_ptr<QwtColorMap> makeColorMap();
    analysis::AnalysisObjectPtr analysisObject() const override { return sink; }

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