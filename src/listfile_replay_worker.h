#ifndef __MVME_LISTFILE_REPLAY_WORKER_H__
#define __MVME_LISTFILE_REPLAY_WORKER_H__

#include "libmvme_export.h"

#include "data_buffer_queue.h"
#include "listfile_replay.h"

class LIBMVME_EXPORT ListfileReplayWorker: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(const DAQState &);
        void replayStopped();
        void replayPaused();

    public:
        using LoggerFun = std::function<void (const QString &)>;

        ListfileReplayWorker(
            ThreadSafeDataBufferQueue *emptyBufferQueue,
            ThreadSafeDataBufferQueue *filledBufferQueue,
            QObject *parent = nullptr);

        void setLogger(LoggerFun logger);

        virtual void setListfile(QIODevice *listifle) = 0;
        virtual DAQStats getStats() const = 0;
        virtual bool isRunning() const = 0;
        virtual DAQState getState() const = 0;
        virtual void setEventsToRead(u32 eventsToRead) = 0;

    public slots:
        // Blocking call which will perform the work
        virtual void start() = 0;

        // Thread-safe calls, setting internal flags to do the state transition
        virtual void stop() = 0;
        virtual void pause() = 0;
        virtual void resume() = 0;

    protected:
        inline ThreadSafeDataBufferQueue *getEmptyQueue() { return m_emptyBufferQueue; }
        inline ThreadSafeDataBufferQueue *getFilledQueue() { return m_filledBufferQueue; }
        inline LoggerFun &getLogger() { return m_logger; }

        inline void logMessage(const QString &msg)
        {
            if (getLogger())
                getLogger()(msg);
        }

    private:
        ThreadSafeDataBufferQueue *m_emptyBufferQueue;
        ThreadSafeDataBufferQueue *m_filledBufferQueue;
        LoggerFun m_logger;
};

#endif /* __MVME_LISTFILE_REPLAY_WORKER_H__ */
