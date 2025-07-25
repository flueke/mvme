#ifndef CACAFEB8_FBBC_4B62_9E7F_AE58BC75338D
#define CACAFEB8_FBBC_4B62_9E7F_AE58BC75338D

#include <QWidget>
#include "analysis_service_provider.h"
#include "libmvme_export.h"

class LIBMVME_EXPORT HistogramOperationsWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit HistogramOperationsWidget(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~HistogramOperationsWidget() override;

        void setHistoOp(const std::shared_ptr<HistogramOperation> &op);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* CACAFEB8_FBBC_4B62_9E7F_AE58BC75338D */
