#ifndef __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_
#define __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_

#include <QWidget>
#include "analysis_service_provider.h"
#include "histo_ui.h"
#include "mvme_qwt.h"
#include "libmvme_export.h"

// TODO: add a removeEnty() or popEntry() method to MultiPlotWidget. Note:
// PlotEntry is not public. It's a detail and related to TilePlot.
// Need some way to refer to entries. Must surive relayout operations, etc.

class LIBMVME_EXPORT MultiPlotWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit MultiPlotWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~MultiPlotWidget() override;

        bool eventFilter(QObject *watched, QEvent *event) override;
        int getReplotPeriod() const; // in ms

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

        void setReplotPeriod(int ms); // negative values disable automatic replotting
        void replot();
        size_t getNumberOfEntries() const;

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

#endif // __MNT_DATA_SRC_MVME2_SRC_MULTIPLOT_WIDGET_H_
