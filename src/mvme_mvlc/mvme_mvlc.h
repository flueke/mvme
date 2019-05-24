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
// Note: "Ready" state not used for now
// Valid transitions are:
//   Idle     -> Starting
//   Starting -> Ready | Idle
//   Ready    -> Running | Idle
//   Running  -> Paused | Stopping
//   Paused   -> Runnning | Stopping
//   Stopping -> Idle

enum class ReadoutState
{
    Idle,
    Starting,
    //Ready,
    Running,
    Paused,
    Stopping,
};

Q_DECLARE_METATYPE(ReaderState)

struct ReadoutContext
{
    const QString runId;
    std::unique_ptr<QIODevice> listfileOut;
    const VMEConfig *vmeConfig;
};

// TODO: use this to have a way to distinguish buffers from MVLC_ETH/USB
struct TaggedBuffer
{
    int tag;
    QVector<u8> buffer;
};

class ReadoutInterface: public QObject
{
    Q_OBJECT
    signals:
        void started();
        //void ready();
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
        // stop() has been invoked from the outisde.
        void start(const ReadoutContext &ctx);

        // Thread safe, sets an atomic flag which makes readoutLoop() return.
        void stop();

        // Thread safe, sets atomic flag which makes readoutLoop() copy the
        // next buffer it receives and send it out via the bufferReady()
        // signal.
        void requestNextBuffers(size_t count);

    protected:
        bool isNextBufferRequested() { return m_isNextBufferRequested; }
        void requestedBufferReady(const QVector<u8> &buffer);

    private:
        std::atomic<size_t> m_requestedBuffers;
};

}
}


#endif /* __MVME_MVLC_H__ */
