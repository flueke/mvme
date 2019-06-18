#ifndef __MVME_MVME_LISTFILE_WORKER_H__
#define __MVME_MVME_LISTFILE_WORKER_H__

#include "listfile_replay_worker.h"
#include "mvme_listfile_utils.h"

class LIBMVME_EXPORT MVMEListfileWorker: public ListfileReplayWorker
{
    Q_OBJECT
    public:
        using LoggerFun = std::function<void (const QString &)>;

        MVMEListfileWorker(
            ThreadSafeDataBufferQueue *emptyBufferQueue,
            ThreadSafeDataBufferQueue *filledBufferQueue,
            QObject *parent = nullptr);

        ~MVMEListfileWorker() override;

        void setListfile(QIODevice *listfile, ListfileBufferFormat format) override;

        DAQStats getStats() const override { return m_stats; }
        bool isRunning() const override { return m_state != DAQState::Idle; }
        DAQState getState() const override { return m_state; }

        void setEventsToRead(u32 eventsToRead) override;

    public slots:
        // Blocking call which will perform the work
        void start() override;

        // Thread-safe calls, setting internal flags to do the state transition
        void stop() override;
        void pause() override;
        void resume() override;

    private:
        void mainLoop();
        void setState(DAQState state);
        void logMessage(const QString &str);

        DAQStats m_stats;

        std::atomic<DAQState> m_state;
        std::atomic<DAQState> m_desiredState;

        ListFile m_listfile;

        qint64 m_bytesRead = 0;
        qint64 m_totalBytes = 0;

        u32 m_eventsToRead = 0;
        bool m_logBuffers = false;
};

#endif /* __MVME_MVME_LISTFILE_WORKER_H__ */
