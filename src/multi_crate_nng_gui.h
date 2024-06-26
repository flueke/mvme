#ifndef BA8142A2_50D3_441C_B32C_AED051A37E0D
#define BA8142A2_50D3_441C_B32C_AED051A37E0D

#include "multi_crate_nng.h"

namespace mesytec::mvme::multi_crate
{

class LIBMVME_EXPORT CratePipelineMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        CratePipelineMonitorWidget(QWidget* parent = nullptr);
        ~CratePipelineMonitorWidget() override;

        void setPipeline(const std::string &name, const std::vector<CratePipelineStep> &pipeline);
        void removePipeline(const std::string &name);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

// Use Qt::QueuedConnection with this to ensure that connected slots are called in the main thread.
class LIBMVME_EXPORT QtJobObserver: public QObject, public JobObserverInterface
{
    Q_OBJECT

    signals:
        void jobStarted(const std::shared_ptr<JobContextInterface> &context);
        void jobFinished(const std::shared_ptr<JobContextInterface> &context);

    public:
        QtJobObserver(QObject *parent = nullptr)
            : QObject(parent)
        {
            qRegisterMetaType<std::shared_ptr<JobContextInterface>>("std::shared_ptr<JobContextInterface>");
        }

        void onJobStarted(const std::shared_ptr<JobContextInterface> &context) override
        {
            emit jobStarted(context);
        }

        void onJobFinished(const std::shared_ptr<JobContextInterface> &context) override
        {
            emit jobFinished(context);
        }
};

#if 0
class ReplayAppGui: public QWidget
{
    Q_OBJECT

    public:
        ReplayAppGui(QWidget* parent = nullptr);
        ~ReplayAppGui();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
#endif


}

#endif /* BA8142A2_50D3_441C_B32C_AED051A37E0D */
