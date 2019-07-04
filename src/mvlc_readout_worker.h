#ifndef __MVME_MVLC_READOUT_WORKER_H__
#define __MVME_MVLC_READOUT_WORKER_H__

#include "vme_readout_worker.h"
#include "vme_daq.h"
#include "mvme_stream_util.h"

struct MVLCReadoutCounters
{
    // MVLC_USB only (ETH does not perform frame type checks as incoming data
    // is packetized as is).
    // Unexpected frame types encountered. The readout should produce frames of
    // types StackFrame and StackContinuation only.
    u64 frameTypeErrors;

    // MVLC_USB only.
    // Total number of bytes from partial frames. This is the amount of data
    // moved from the end of incoming read buffers into temp storage and then
    // reused at the start of the next buffer.
    u64 partialFrameTotalBytes;
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
};

#endif /* __MVME_MVLC_READOUT_WORKER_H__ */
