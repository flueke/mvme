#ifndef __MVME_HISTO_UI_H__
#define __MVME_HISTO_UI_H__

#include <memory>
#include <QWidget>
#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_picker.h>
#include <qwt_point_data.h>
#include <qwt_samples.h>
#include <qwt_series_data.h>

#include "histo1d.h"

class QToolBar;
class QStatusBar;

namespace histo_ui
{

QRectF canvas_to_scale(const QwtPlot *plot, const QRect &rect);
QPointF canvas_to_scale(const QwtPlot *plot, const QPoint &pos);

class PlotWidget: public QWidget
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

        QwtPlot *getPlot();
        const QwtPlot *getPlot() const;

        QToolBar *getToolBar();
        QStatusBar *getStatusBar();

    public slots:
        void replot();

    protected:
        bool eventFilter(QObject * object, QEvent *event) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class PlotPicker: public QwtPlotPicker
{
    public:
        using QwtPlotPicker::QwtPlotPicker;

        // make the protected QwtPlotPicker::reset() public
        void reset() override
        {
            QwtPlotPicker::reset();
        }
};

class NewIntervalPicker: public PlotPicker
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

class IntervalEditorPicker: public PlotPicker
{
    Q_OBJECT
    signals:
        void intervalModified(const QwtInterval &interval);

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

bool is_linear_axis_scale(const QwtPlot *plot, QwtPlot::Axis axis);
bool is_logarithmic_axis_scale(const QwtPlot *plot, QwtPlot::Axis axis);

class PlotAxisScaleChanger: public QObject
{
    Q_OBJECT
    public:
        PlotAxisScaleChanger(QwtPlot *plot, QwtPlot::Axis axis);

        bool isLinear() const;
        bool isLogarithmic() const;

    public slots:
        void setLinear();
        void setLogarithmic();

    private:
        QwtPlot *m_plot;
        QwtPlot::Axis m_axis;
};

class Histo1DIntervalData: public QwtSeriesData<QwtIntervalSample>
{
    public:
        explicit Histo1DIntervalData(Histo1D *histo)
            : QwtSeriesData<QwtIntervalSample>()
            , m_histo(histo)
        {
            assert(histo);
        }

        virtual size_t size() const override
        {
            return m_histo->getNumberOfBins(m_rrf);
        }

        virtual QwtIntervalSample sample(size_t i) const override
        {
            auto result = QwtIntervalSample(
                m_histo->getBinContent(i, m_rrf),
                m_histo->getBinLowEdge(i, m_rrf),
                m_histo->getBinLowEdge(i+1, m_rrf));

            return result;
        }

        virtual QRectF boundingRect() const override
        {
            // Qt and Qwt have different understanding of rectangles. For Qt
            // it's top-down like screen coordinates, for Qwt it's bottom-up
            // like the coordinates in a plot.
            //auto result = QRectF(
            //    m_histo->getXMin(),  m_histo->getMaxValue(), // top-left
            //    m_histo->getWidth(), m_histo->getMaxValue());  // width, height
            auto result = QRectF(
                m_histo->getXMin(), 0.0,
                m_histo->getWidth(), m_histo->getMaxValue(m_rrf));

            return result;
        }

        void setResolutionReductionFactor(u32 rrf) { m_rrf = rrf; }
        u32 getResolutionReductionFactor() const { return m_rrf; }

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

class Histo1DGaussCurveData: public QwtSyntheticPointData
{
    public:
        static constexpr size_t NumberOfPoints = 500;

        Histo1DGaussCurveData()
            : QwtSyntheticPointData(NumberOfPoints)
        {
        }

        virtual double y(double x) const override
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

}

#endif /* __MVME_HISTO_UI_H__ */
