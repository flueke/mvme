#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_

#include <QWidget>
#include "analysis_service_provider.h"
#include "histo_ui.h"
#include "mvme_qwt.h"

class MultiPlotWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~MultiPlotWidget() override;

        bool eventFilter(QObject *watched, QEvent *event) override;

    public slots:
        // Add all plots for the given sink
        void addSink(const analysis::SinkPtr &sink);

        // For adding single sink elements (histograms, rate monitors, ...)
        void addSinkElement(const analysis::SinkPtr &sink, int elementIndex);

        // Add a non-sink, single histogram entry.
        void addHisto1D(const Histo1DPtr &histo);

        void setMaxVisibleResolution(size_t maxres);
        size_t getMaxVisibleResolution() const;

        // Replaces this widgets contents based on the data stored in view.
        void loadView(const std::shared_ptr<analysis::PlotGridView> &view);

        void clear(); // removes all entries

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
