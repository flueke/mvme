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

        void addPipeline(const std::string &name, const std::vector<CratePipelineStep> &pipeline);
        void removePipeline(const std::string &name);

    private:
        struct Private;
        std::unique_ptr<Private> d;
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

class JobRuntimeWatcher: public QObject
{
    Q_OBJECT
    signals:
        void finished();

    public:
        JobRuntimeWatcher(JobRuntime &rt, QObject *parent = nullptr)
            : QObject(parent)
            , rt_(rt)
        {
            connect(&timer_, &QTimer::timeout, this, &JobRuntimeWatcher::check);
        }

    public slots:
        void start()
        {
            timer_.start(100);
        }

    private slots:
        void check()
        {
            if (!rt_.isRunning())
            {
                timer_.stop();
                emit finished();
            }
        }

    private:
        JobRuntime &rt_;
        QTimer timer_;

};

class JobWatcher: public QObject
{
    Q_OBJECT
    signals:
        void finished();

    public:
        JobWatcher(const std::weak_ptr<JobContextInterface> &jobContext, QObject *parent = nullptr)
            : QObject(parent)
            , jobContext_(jobContext)
        {
            connect(&timer_, &QTimer::timeout, this, &JobWatcher::check);
        }

    public slots:
        void start()
        {
            timer_.start(100);
        }

        void setJobContext(const std::weak_ptr<JobContextInterface> &jobContext)
        {
            jobContext_ = jobContext;
        }

    private slots:
        void check()
        {
            if (auto ctx = jobContext_.lock())
            {
                if (ctx->jobRuntime().isRunning())
                {
                    timer_.stop();
                    emit finished();
                }
            }
        }

    private:
        std::weak_ptr<JobContextInterface> jobContext_;
        QTimer timer_;

};


}

#endif /* BA8142A2_50D3_441C_B32C_AED051A37E0D */
