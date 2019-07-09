#ifndef __MVME_MVLC_READOUT_WORKER_H__
#define __MVME_MVLC_READOUT_WORKER_H__

#include "mvlc/mvlc_constants.h"
#include "mvme_stream_util.h"
#include "vme_daq.h"
#include "vme_readout_worker.h"

struct MVLCReadoutCounters
{
    // MVLC_USB only (ETH does not perform frame type checks because incoming
    // data is packetized as is).
    // The number of unexpected frame types encountered. The readout should
    // produce outer frames of types StackFrame and StackContinuation only.
    u64 frameTypeErrors;

    // MVLC_USB only.
    // Total number of bytes from partial frames. This is the amount of data
    // moved from the end of incoming read buffers into temp storage and then
    // reused at the start of the next buffer.
    u64 partialFrameTotalBytes;

    // The counters are the same as the mvlc frame_flags except for
    // NotAStackError which counts the number of non-0xF7 frames received via
    // the notification channel.

    static const u8 NumErrorCounters = mesytec::mvlc::frame_flags::shifts::SyntaxError + 1;
    using ErrorCounters = std::array<u64, NumErrorCounters>;

    // Counts the flags set in the 0xF7 stack error notifications sent out on
    // the command pipe. These notifications are polled by MVLC_VMEController
    // and handled by the MVLCReadoutWorker.
    std::array<ErrorCounters, mesytec::mvlc::stacks::StackCount> stackErrors;

    // The number of non 0xF7 frames received via polling. Should be zero at
    // all times.
    u64 nonStackErrorNotifications;
};

namespace mesytec
{
namespace mvlc
{
    class MVLC_VMEController;
}
}

class MVLCReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    public:
        MVLCReadoutWorker(QObject *parent = nullptr);
        ~MVLCReadoutWorker() override;

        void start(quint32 cycles = 0) override;
        void stop() override;
        void pause() override;
        void resume(quint32 cycles = 0) override;
        bool isRunning() const override;
        DAQState getState() const override;

        MVLCReadoutCounters getReadoutCounters() const;
        mesytec::mvlc::MVLC_VMEController *getMVLC();

    private:
        struct Private;
        std::unique_ptr<Private> d;

        void readoutLoop();
        void setState(const DAQState &state);
        void logError(const QString &msg);
        void timetick();
        void pauseDAQ();
        void resumeDAQ();
        std::error_code readAndProcessBuffer(size_t &bytesTransferred);
        std::error_code readout_eth(size_t &bytesTransferred);
        std::error_code readout_usb(size_t &bytesTransferred);

        DataBuffer *getOutputBuffer();
        void maybePutBackBuffer();
        void flushCurrentOutputBuffer();
        //void handleStackErrorNotification(const QVector<u32> &data);
};

#endif /* __MVME_MVLC_READOUT_WORKER_H__ */
