#ifndef __MVME_MVLC_H__
#define __MVME_MVLC_H__

#include "vme_config.h"

namespace mesytec
{
namespace mvme
{

enum class MessageSeverity
{
    Info,
    Warning,
    Error
};

Q_DECLARE_METATYPE(MessageSeverity)

// State of the data producing side. Used for actual DAQ readouts and for
// replays from file.
// Valid transitions are:
//   Idle     -> Starting
//   Starting -> Ready | Idle
//   Ready    -> Running | Stopping
//   Running  -> Paused | Stopping
//   Paused   -> Runnning | Stopping
//   Stopping -> Idle

enum class ReadoutState
{
    Idle,
    Starting,
    Ready,
    Running,
    Paused,
    Stopping,
};

Q_DECLARE_METATYPE(ReadoutState)

enum class StartupOption
{
    WaitWhenReady,
    StartImmediately
};

struct ReadoutContext
{
    const QString runId;
    std::unique_ptr<QIODevice> listfileOut;
    const VMEConfig *vmeConfig;
};

class ReadoutInterface: public QObject
{
    Q_OBJECT
    signals:
        void starting();
        void ready();
        void running();
        void paused();
        void stopped();

        void bufferReady(const QVector<u8> &buffer);
        void logMessage(const MessageSeverity &sev, const QString &msg);

    public:
        ReadoutInterface(QObject *parent = nullptr);

    public slots:
        // The main entry point to start a readout. This is a blocking call
        // which returns either when a fatal startup error occurs or after
        // stop() has been invoked from the outside.
        void start(StartupOption opt);

        // Use this to transition from Ready state if
        // StartupOption::WaitWhenReady was used.
        void continueStartup();

        // Tells the readout to stop. Thread safe.
        void stop();

        // Tells the data reader to make the next count buffers it reads
        // available via the bufferReady() signal.
        // Thread safe.
        void requestNextBuffers(int count)
        {
            m_requestedBuffers = count;
        }

        size_t requestedBufferCount() const
        {
            return m_requestedBuffers;
        }

    protected:

    private:
        std::atomic<int> m_requestedBuffers;
};

}
}

#endif /* __MVME_MVLC_H__ */
