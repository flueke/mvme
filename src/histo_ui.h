#ifndef __MVME_HISTO_UI_H__
#define __MVME_HISTO_UI_H__

#include <memory>
#include <QWidget>
#include <qwt_plot.h>
#include <qwt_plot_picker.h>

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

    public:
        PlotWidget(QWidget *parent = nullptr);
        ~PlotWidget() override;

        QwtPlot *getPlot();
        const QwtPlot *getPlot() const;

        QToolBar *getToolBar();
        QStatusBar *getStatusBar();

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

}

#endif /* __MVME_HISTO_UI_H__ */
