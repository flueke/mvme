#ifndef __MVLC_LISTFILE_WORKER_H__
#define __MVLC_LISTFILE_WORKER_H__

#include "listfile_replay_worker.h"

class LIBMVME_EXPORT MVLCListfileWorker: public ListfileReplayWorker
{
    Q_OBJECT
    public:
        using LoggerFun = std::function<void (const QString &)>;

        MVLCListfileWorker(
            ThreadSafeDataBufferQueue *emptyBufferQueue,
            ThreadSafeDataBufferQueue *filledBufferQueue,
            QObject *parent = nullptr);

        ~MVLCListfileWorker() override;

        void setListfile(QIODevice *listifle, ListfileBufferFormat format) override;

        DAQStats getStats() const override;
        bool isRunning() const override;
        DAQState getState() const override;
        void setEventsToRead(u32 eventsToRead) override;

    public slots:
        // Blocking call which will perform the work
        void start() override;

        // Thread-safe calls, setting internal flags to do the state transition
        void stop() override;
        void pause() override;
        void resume() override;

    private:
        void setState(DAQState state);

        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVLC_LISTFILE_WORKER_H__ */
