#ifndef CACAFEB8_FBBC_4B62_9E7F_AE58BC75338D
#define CACAFEB8_FBBC_4B62_9E7F_AE58BC75338D

#include <QWidget>
#include "analysis_fwd.h"
#include "analysis_service_provider.h"
#include "histo_ui.h"
#include "libmvme_export.h"

class LIBMVME_EXPORT HistogramOperationsWidget: public histo_ui::IPlotWidget
{
    Q_OBJECT
    public:
        explicit HistogramOperationsWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~HistogramOperationsWidget() override;

        void setHistoOp(const std::shared_ptr<analysis::HistogramOperation> &op);
        std::shared_ptr<analysis::HistogramOperation> getHistoOp() const;

        QwtPlot *getPlot() override;
        const QwtPlot *getPlot() const override;
        QToolBar *getToolBar() override;
        QStatusBar *getStatusBar() override;

        AnalysisServiceProvider *getServiceProvider() const;

    public slots:
        void replot() override;

    protected:
        void dragEnterEvent(QDragEnterEvent *ev) override;
        void dropEvent(QDropEvent *ev) override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* CACAFEB8_FBBC_4B62_9E7F_AE58BC75338D */
