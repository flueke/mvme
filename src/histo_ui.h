#ifndef __MVME_HISTO_UI_H__
#define __MVME_HISTO_UI_H__

#include <memory>
#include <QWidget>
#include <QwtPlot>

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

}

#endif /* __MVME_HISTO_UI_H__ */
