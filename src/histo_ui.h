#ifndef __MVME_HISTO_UI_H__
#define __MVME_HISTO_UI_H__

#include <memory>
#include <QComboBox>
#include <QWidget>
#include <qwt_color_map.h>
#include <qwt_interval.h>
#include <qwt_matrix_raster_data.h>
#include <qwt_picker_machine.h>
#include <qwt_plot.h>
#include <qwt_plot_picker.h>
#include <qwt_point_data.h>
#include <qwt_samples.h>
#include <qwt_series_data.h>

#include "histo1d.h"
#include "histo2d.h"
#include "libmvme_export.h"

class QToolBar;
class QStatusBar;
class QwtPlotZoomer;

namespace histo_ui
{

QRectF canvas_to_scale(const QwtPlot *plot, const QRect &rect);
QPointF canvas_to_scale(const QwtPlot *plot, const QPoint &pos);

class LIBMVME_EXPORT IPlotWidget: public QWidget
{
    Q_OBJECT
    public:
        using QWidget::QWidget;
        ~IPlotWidget() override;

        virtual QwtPlot *getPlot() = 0;
        virtual const QwtPlot *getPlot() const = 0;

        virtual QToolBar *getToolBar() = 0;
        virtual QStatusBar *getStatusBar() = 0;

    public slots:
        virtual void replot() = 0;
};

class LIBMVME_EXPORT PlotWidget: public IPlotWidget
{
    Q_OBJECT
    signals:
        void mouseEnteredPlot();
        void mouseLeftPlot();
        void mouseMoveOnPlot(const QPointF &pos);
        void aboutToReplot();

    public:
        PlotWidget(QWidget *parent = nullptr);
        ~PlotWidget() override;

        QwtPlot *getPlot() override;
        const QwtPlot *getPlot() const override;

        QToolBar *getToolBar() override;
        QStatusBar *getStatusBar() override;

    public slots:
        void replot() override;

    protected:
        bool eventFilter(QObject * object, QEvent *event) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class LIBMVME_EXPORT PlotPicker: public QwtPlotPicker
{
    Q_OBJECT
    signals:
        // Overloads QwtPicker::removed(const QPoint &).
        // Emitted when the last appended point of a selection is removed.
        void removed(const QPointF &p);

    public:
        explicit PlotPicker(QWidget *canvas);

        PlotPicker(int xAxis, int yAxis,
                   RubberBand rubberBand,
                   DisplayMode trackerMode,
                   QWidget *canvas);

        // make the protected QwtPlotPicker::reset() public
        void reset() override
        {
            QwtPlotPicker::reset();
        }

    private slots:
        void onPointRemoved(const QPoint &p);
};

class LIBMVME_EXPORT NewIntervalPicker: public PlotPicker
{
    Q_OBJECT
    signals:
        void intervalSelected(const QwtInterval &interval);
        void canceled();

    public:
        NewIntervalPicker(QwtPlot *plot);
        ~NewIntervalPicker() override;

    public slots:
        // Reset state, hide markers
        void reset() override;

        // Calls reset, then emits canceled();
        void cancel();

    protected:
        void transition(const QEvent *event) override;

    private slots:
        void onPointSelected(const QPointF &p);
        void onPointMoved(const QPointF &p);
        void onPointAppended(const QPointF &p);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class LIBMVME_EXPORT IntervalEditorPicker: public PlotPicker
{
    Q_OBJECT
    signals:
        void intervalModified(const QwtInterval &interval);

        // Emitted in reaction to mouse move events. If the cursor is close
        // enough to grab a border 'wouldGrab' is true, otherwise if moving away
        // from a border it is set to false.
        void mouseWouldGrabIntervalBorder(bool wouldGrab);

    public:
        IntervalEditorPicker(QwtPlot *plot);
        ~IntervalEditorPicker() override;

        void setInterval(const QwtInterval &interval);
        void reset() override;

    protected:
        void widgetMousePressEvent(QMouseEvent *) override;
        void widgetMouseReleaseEvent(QMouseEvent *) override;
        void widgetMouseMoveEvent(QMouseEvent *) override;

    private slots:
        void onPointMoved(const QPointF &p);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

// Allows to remove the last placed point by pressing mouse button 3.
class LIBMVME_EXPORT ImprovedPickerPolygonMachine: public QwtPickerPolygonMachine
{
    using QwtPickerPolygonMachine::QwtPickerPolygonMachine;

    QList<Command> transition(const QwtEventPattern &ep, const QEvent *ev) override;
};

class LIBMVME_EXPORT PolygonEditorPicker: public PlotPicker
{
    Q_OBJECT
    signals:
        // Emitted when a point or and edge has been moved, a point has been
        // inserted/removed, or the polygon has been panned.
        void polygonModified(const QPolygonF &poly);

        void mouseWouldGrabSomething(bool wouldGrab);

        // Emitted when a drag/pan operation starts/ends
        void beginModification();
        void endModification();

    public:
        PolygonEditorPicker(QwtPlot *plot);
        ~PolygonEditorPicker() override;

        void setPolygon(const QPolygonF &poly);
        void reset() override;

    protected:
        void widgetMousePressEvent(QMouseEvent *) override;
        void widgetMouseReleaseEvent(QMouseEvent *) override;
        void widgetMouseMoveEvent(QMouseEvent *) override;

    private slots:
        void onPointMoved(const QPointF &p);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

enum class AxisScaleType
{
    Linear,
    Logarithmic
};

LIBMVME_EXPORT bool is_linear_axis_scale(const QwtPlot *plot, QwtPlot::Axis axis);
LIBMVME_EXPORT bool is_logarithmic_axis_scale(const QwtPlot *plot, QwtPlot::Axis axis);
LIBMVME_EXPORT bool is_linear_axis_scale(const IPlotWidget *plot, QwtPlot::Axis axis);
LIBMVME_EXPORT bool is_logarithmic_axis_scale(const IPlotWidget *plot, QwtPlot::Axis axis);
LIBMVME_EXPORT AxisScaleType get_axis_scale_type(const QwtPlot *plot, QwtPlot::Axis axis);

class LIBMVME_EXPORT PlotAxisScaleChanger: public QObject
{
    Q_OBJECT
    public:
        PlotAxisScaleChanger(QwtPlot *plot, QwtPlot::Axis axis);

        bool isLinear() const;
        bool isLogarithmic() const;

    public slots:
        void setLinear();
        void setLogarithmic();

        inline void setScaleType(AxisScaleType t)
        {
            if (t == AxisScaleType::Linear)
                setLinear();
            else
                setLogarithmic();
        }

    private:
        QwtPlot *m_plot;
        QwtPlot::Axis m_axis;
};

class LIBMVME_EXPORT Histo1DIntervalData: public QwtSeriesData<QwtIntervalSample>
{
    public:
        explicit Histo1DIntervalData(Histo1D *histo)
            : QwtSeriesData<QwtIntervalSample>()
            , m_histo(histo)
        {
            assert(histo);
        }

        size_t size() const override
        {
            return m_histo->getNumberOfBins(m_rrf);
        }

        QwtIntervalSample sample(size_t i) const override
        {
            auto result = QwtIntervalSample(
                m_histo->getBinContent(i, m_rrf),
                m_histo->getBinLowEdge(i, m_rrf),
                m_histo->getBinLowEdge(i+1, m_rrf));

            return result;
        }

        QRectF boundingRect() const override
        {
            auto xMin = m_histo->getXMin();
            auto yMin = m_histo->getMinValue(m_rrf);
            auto yMax = m_histo->getMaxValue(m_rrf);
            auto width = m_histo->getWidth();
            auto height = yMax - yMin;

            QRectF result(xMin, yMin, width, height);
            return result;
        }

        void setResolutionReductionFactor(u32 rrf) { m_rrf = rrf; }
        u32 getResolutionReductionFactor() const { return m_rrf; }

        Histo1D *getHisto() { return m_histo; }
        const Histo1D *getHisto() const { return m_histo; }

    private:
        Histo1D *m_histo;
        u32 m_rrf = Histo1D::NoRR;
};

/* Calculates a gauss fit using the currently visible maximum histogram value.
 *
 * Note: The resolution is independent of the underlying histograms resolution.
 * Instead NumberOfPoints samples are used at all zoom levels.
 */
static constexpr double FWHMSigmaFactor = 2.3548;

class LIBMVME_EXPORT Histo1DGaussCurveData: public QwtSyntheticPointData
{
    public:
        static constexpr size_t NumberOfPoints = 500;

        Histo1DGaussCurveData()
            : QwtSyntheticPointData(NumberOfPoints)
        {
        }

        double y(double x) const override
        {
            double s = m_stats.fwhm / FWHMSigmaFactor;
            // Instead of using the center of the max bin the center point
            // between the fwhm edges is used. This makes the curve remain in a
            // much more stable x-position.
            double a = m_stats.fwhmCenter;

            // This is (1.0 / (SqrtPI2 * s)) if the resulting area should be 1.
            double firstTerm  = m_stats.maxValue;
            double exponent   = -0.5 * ((squared(x - a) / squared(s)));
            double secondTerm = std::exp(exponent);
            double yValue     = firstTerm * secondTerm;

            //qDebug("x=%lf, s=%lf, a=%lf, stats.maxBin=%d",
            //       x, s, a, m_stats.maxBin);
            //qDebug("firstTerm=%lf, exponent=%lf, secondTerm=%lf, yValue=%lf",
            //       firstTerm, exponent, secondTerm, yValue);

            return yValue;
        }

        void setStats(Histo1DStatistics stats)
        {
            m_stats = stats;
        }

        static inline double squared(double x)
        {
            return x * x;
        }

    private:
        Histo1DStatistics m_stats;
};

struct BasicRasterData: public QwtMatrixRasterData
{
    ResolutionReductionFactors m_rrf;

#ifndef QT_NO_DEBUG
    /* Counts the number of samples obtained by qwt when doing a replot. Has to be atomic
     * as QwtPlotSpectrogram::renderImage() uses threaded rendering internally.
     * The number of samples heavily depends on the result of the pixelHint() method and
     * is performance critical. */
    mutable std::atomic<u64> m_sampledValuesForLastReplot;
#endif

    BasicRasterData()
#ifndef QT_NO_DEBUG
        : m_sampledValuesForLastReplot(0u)
#endif
    {}

    void setResolutionReductionFactors(u32 rrfX, u32 rrfY) { m_rrf = { rrfX, rrfY }; }
    void setResolutionReductionFactors(const ResolutionReductionFactors &rrf) { m_rrf = rrf; }

    virtual void initRaster(const QRectF &area, const QSize &raster) override
    {
        preReplot();
        QwtRasterData::initRaster(area, raster);
    }

    virtual void discardRaster() override
    {
        postReplot();
        QwtRasterData::discardRaster();
    }

    void preReplot()
    {
#ifndef QT_NO_DEBUG
        //qDebug() << __PRETTY_FUNCTION__ << this;
        m_sampledValuesForLastReplot = 0u;
#endif
    }

    void postReplot()
    {
#ifndef QT_NO_DEBUG
        //qDebug() << __PRETTY_FUNCTION__ << this
        //    << "sampled values for last replot: " << m_sampledValuesForLastReplot;
#endif
    }
};

struct Histo2DRasterData: public BasicRasterData
{
    Histo2D *m_histo;

    explicit Histo2DRasterData(Histo2D *histo)
        : BasicRasterData()
        , m_histo(histo)
    {
    }

    virtual double value(double x, double y) const override
    {
#ifndef QT_NO_DEBUG
        m_sampledValuesForLastReplot++;
#endif
        //qDebug() << __PRETTY_FUNCTION__ << this
        //    << "x" << x << ", y" << y;

        double v = m_histo->getValue(x, y, m_rrf);
        double r = (v > 0.0 ? v : mesytec::mvme::util::make_quiet_nan());
        return r;
    }

    virtual QRectF pixelHint(const QRectF &) const override
    {
        QRectF result
        {
            0.0, 0.0,
            m_histo->getAxisBinning(Qt::XAxis).getBinWidth(m_rrf.x),
            m_histo->getAxisBinning(Qt::YAxis).getBinWidth(m_rrf.y)
        };

        //qDebug("%s: rrfs: x=%d, y=%d, binWidths: x=%lf, y=%lf",
        //       __PRETTY_FUNCTION__, m_rrf.x, m_rrf.y,
        //       m_histo->getAxisBinning(Qt::XAxis).getBinWidth(m_rrf.x),
        //       m_histo->getAxisBinning(Qt::YAxis).getBinWidth(m_rrf.y));
        //qDebug() << __PRETTY_FUNCTION__ << ">>>>>>" << m_histo << result;

        return result;
    }

};

using HistoList = QVector<std::shared_ptr<Histo1D>>;

struct Histo1DListRasterData: public BasicRasterData
{
    HistoList m_histos;

    explicit Histo1DListRasterData(const HistoList &histos)
        : BasicRasterData()
        , m_histos(histos)
    {}

    virtual double value(double x, double y) const override
    {
#ifndef QT_NO_DEBUG
        m_sampledValuesForLastReplot++;
#endif
        int histoIndex = x;

        if (histoIndex < 0 || histoIndex >= m_histos.size())
            return mesytec::mvme::util::make_quiet_nan();

        double v = m_histos[histoIndex]->getValue(y, m_rrf.y);
        double r = (v > 0.0 ? v : mesytec::mvme::util::make_quiet_nan());
        return r;
    }

    virtual QRectF pixelHint(const QRectF &/*area*/) const override
    {
        double sizeX = 1.0;
        double sizeY = 1.0;

        if (!m_histos.isEmpty())
        {
            sizeY = m_histos[0]->getBinWidth(m_rrf.y);
        }

        QRectF result
        {
            0.0, 0.0, sizeX, sizeY
        };

        //qDebug() << __PRETTY_FUNCTION__ << ">>>>>>" << result;

        return result;
    }
};

LIBMVME_EXPORT void setup_axis_scale_changer(PlotWidget *w, QwtPlot::Axis axis, const QString &axisText);
LIBMVME_EXPORT std::unique_ptr<QwtLinearColorMap> make_histo2d_color_map(AxisScaleType scaleType);
LIBMVME_EXPORT QwtText make_qwt_text(const QString &str, int fontPointSize = 10);

template<typename Context, typename Functor>
QAction *install_checkable_toolbar_action(
    PlotWidget *w, const QString &label, const QString &actionName,
    Context context, Functor functor)
{
    auto toolbar = w->getToolBar();
    auto action = toolbar->addAction(label);
    action->setObjectName(actionName);
    action->setCheckable(true);
    QObject::connect(action, &QAction::toggled, context, functor);
    return action;
}

LIBMVME_EXPORT QwtPlotZoomer *install_scrollzoomer(PlotWidget *w);
LIBMVME_EXPORT NewIntervalPicker *install_new_interval_picker(PlotWidget *w);
LIBMVME_EXPORT IntervalEditorPicker *install_interval_editor(PlotWidget *w);
LIBMVME_EXPORT QwtPlotPicker *install_poly_picker(PlotWidget *w);
LIBMVME_EXPORT QwtPlotPicker *install_tracker_picker(PlotWidget *w);
LIBMVME_EXPORT QwtPlotPicker *install_clickpoint_picker(PlotWidget *w);
LIBMVME_EXPORT QwtPlotPicker *install_dragpoint_picker(PlotWidget *w);
LIBMVME_EXPORT QActionGroup *group_picker_actions(PlotWidget *w);

LIBMVME_EXPORT QwtPlotZoomer *get_zoomer(QWidget *w);

LIBMVME_EXPORT void debug_watch_plot_pickers(QWidget *w);
LIBMVME_EXPORT void watch_mouse_move(PlotWidget *w);

// Scale the y-axis by 5% to have some margin to the top and bottom of the
// widget. Mostly to make the zoomers top scrollbar not overlap the plotted
// graph.
void adjust_y_axis_scale(QwtPlot *plot, double yMin, double yMax, QwtPlot::Axis axis = QwtPlot::yLeft);

}

#endif /* __MVME_HISTO_UI_H__ */
