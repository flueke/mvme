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

class JobPoller: public QObject
{
    Q_OBJECT
    signals:
        void jobReady(const std::shared_ptr<JobContextInterface> &jobContext);

    public:
        JobPoller(QObject *parent = nullptr)
            : QObject(parent)
            , interval_(16)
        {
        }

    public slots:
        void addJob(const std::shared_ptr<JobContextInterface> &jobContext);
        void loop();
        void quit();

    private:
        std::mutex mutex_;
        std::vector<std::weak_ptr<JobContextInterface>> jobs_;
        std::chrono::milliseconds interval_;
        bool quit_ = false;
};

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

class QtJobObserver: public QObject, public JobObserverInterface
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

}

#endif /* BA8142A2_50D3_441C_B32C_AED051A37E0D */
