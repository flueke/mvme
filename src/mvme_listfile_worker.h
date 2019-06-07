#ifndef __MVME_MVME_LISTFILE_WORKER_H__
#define __MVME_MVME_LISTFILE_WORKER_H__

#include "mvme_listfile_utils.h"

class LIBMVME_EXPORT MVMEListfileWorker: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(DAQState);
        void replayStopped();
        void replayPaused();

    public:
        using LoggerFun = std::function<void (const QString &)>;

        MVMEListfileWorker(DAQStats &stats, QObject *parent = 0);
        ~MVMEListfileWorker();

        void setListFile(ListFile *listFile);
        ListFile *getListFile() const { return m_listFile; }

        bool isRunning() const { return m_state != DAQState::Idle; }
        DAQState getState() const { return m_state; }

        void setEventsToRead(u32 eventsToRead);

        void setLogger(LoggerFun logger) { m_logger = logger; }

        ThreadSafeDataBufferQueue *m_freeBuffers = nullptr;
        ThreadSafeDataBufferQueue *m_fullBuffers = nullptr;

    public slots:
        // Blocking call which will perform the work
        void start();

        // Thread-safe calls, setting internal flags to do the state transition
        void stop();
        void pause();
        void resume();

    private:
        void mainLoop();
        void setState(DAQState state);
        void logMessage(const QString &str);

        DAQStats &m_stats;

        DAQState m_state;
        std::atomic<DAQState> m_desiredState;

        ListFile *m_listFile = 0;

        qint64 m_bytesRead = 0;
        qint64 m_totalBytes = 0;

        u32 m_eventsToRead = 0;
        bool m_logBuffers = false;
        LoggerFun m_logger;
};

#endif /* __MVME_MVME_LISTFILE_WORKER_H__ */
