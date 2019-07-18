#ifndef __STREAM_WORKER_BASE_H__
#define __STREAM_WORKER_BASE_H__

#include <QObject>

#include "libmvme_export.h"
#include "mvme_stream_processor.h" // TODO: get rid of this (IMVMEStreamModuleConsumer,...)
#include "util/leaky_bucket.h"

enum class MVMEStreamWorkerState
{
    Idle,
    Running,
    Paused,
    SingleStepping,
};

Q_DECLARE_METATYPE(MVMEStreamWorkerState);

QString to_string(const MVMEStreamWorkerState &state);

class LIBMVME_EXPORT StreamWorkerBase: public QObject
{
    Q_OBJECT
    signals:
        void started();
        void stopped();
        void stateChanged(MVMEStreamWorkerState);
        void sigLogMessage(const QString &msg);

    public:
        enum class MessageSeverity
        {
            Info,
            Warning,
            Error
        };

        StreamWorkerBase(QObject *parent = nullptr);

        virtual MVMEStreamWorkerState getState() const = 0;

        virtual void setStartPaused(bool startPaused) = 0;
        virtual bool getStartPaused() const = 0;

        virtual void attachBufferConsumer(IMVMEStreamBufferConsumer *consumer) = 0;
        virtual void removeBufferConsumer(IMVMEStreamBufferConsumer *consumer) = 0;

        virtual void attachModuleConsumer(IMVMEStreamModuleConsumer *consumer) = 0;
        virtual void removeModuleConsumer(IMVMEStreamModuleConsumer *consumer) = 0;

        virtual MVMEStreamProcessorCounters getCounters() const = 0;

    public slots:
        // Blocking call. Returns after stop() has been invoked from the outside.
        virtual void start() = 0;

        virtual void stop(bool whenQueueEmpty = true) = 0;
        virtual void pause() = 0;
        virtual void resume() = 0;
        virtual void singleStep() = 0;

        // Used to get module and raw buffer consumers into a "running but
        // idle" state.
        virtual void startupConsumers() = 0;
        virtual void shutdownConsumers() = 0;

    protected:
        // Returns true if the message was logged, false if it was suppressed due
        // to throttling.
        bool logMessage(const MessageSeverity &sev, const QString &msg, bool useThrottle = false);

        bool logInfo(const QString &msg, bool useThrottle = false)
        {
            return logMessage(MessageSeverity::Info, msg, useThrottle);
        }

        bool logWarn(const QString &msg, bool useThrottle = false)
        {
            qDebug() << __PRETTY_FUNCTION__ << msg;
            return logMessage(MessageSeverity::Warning, msg, useThrottle);
        }

        bool logError(const QString &msg, bool useThrottle = false)
        {
            return logMessage(MessageSeverity::Error, msg, useThrottle);
        }

    private:
        static const int MaxLogMessagesPerSecond = 5;
        LeakyBucketMeter m_logThrottle;

};

#endif /* __STREAM_WORKER_BASE_H__ */
