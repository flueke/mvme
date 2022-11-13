#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_

#include <QWidget>
#include "analysis_service_provider.h"

class MultiPlotWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~MultiPlotWidget() override;

        bool eventFilter(QObject *watched, QEvent *event) override;

    public slots:
        void addSink(const analysis::SinkPtr &sink);
        // For adding single 1d histograms and rate monitors
        void addSinkElement(const analysis::SinkPtr &sink, int elementIndex);

    protected:
        void dragEnterEvent(QDragEnterEvent *ev) override;
        void dragLeaveEvent(QDragLeaveEvent *ev) override;
        void dragMoveEvent(QDragMoveEvent *ev) override;
        void dropEvent(QDropEvent *ev) override;
        void mouseMoveEvent(QMouseEvent *ev) override;
        void mousePressEvent(QMouseEvent *ev) override;
        void mouseReleaseEvent(QMouseEvent *ev) override;
        void wheelEvent(QWheelEvent *ev) override;
        void mouseDoubleClickEvent(QMouseEvent *event) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#if 0
class PlotArrayWidget: public QWidget
{
    Q_OBJECT
    public:
        PlotArrayWidget(QWidget *parent = nullptr);
        ~PlotArrayWidget() override;

        void addSink(const std::shared_ptr<analysis::Histo1DSink> &sink);
        void addSink(const std::shared_ptr<analysis::RateMonitorSink> &sink);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class PlotGalleryWidget: public QWidget
{
    Q_OBJECT
    public:
        PlotGalleryWidget(QWidget *parent = nullptr);
        ~PlotGalleryWidget() override;

        void addSink(const std::shared_ptr<analysis::Histo1DSink> &sink);
        void addSink(const std::shared_ptr<analysis::Histo1DSink> &sink, int histoIndex);
        void addSink(const std::shared_ptr<analysis::RateMonitorSink> &sink);
        void addSink(const std::shared_ptr<analysis::RateMonitorSink> &sink, int monIndex);
        void addSink(const std::shared_ptr<analysis::Histo2DSink> &sink);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
#endif

#endif // __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_