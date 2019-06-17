#ifndef __MVME_MVLC_READOUT_WORKER_H__
#define __MVME_MVLC_READOUT_WORKER_H__

#include "vme_readout_worker.h"
#include "vme_daq.h"
#include "mvme_stream_util.h"

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

        std::atomic<DAQState> m_state;
        std::atomic<DAQState> m_desiredState;
        quint32 m_cyclesToRun = 0;
        bool m_logBuffers = false;
        DataBuffer m_readBuffer;
        DataBuffer m_previousData;
        DataBuffer m_localEventBuffer;
        DataBuffer *m_outputBuffer = nullptr;

        struct EventWithModules
        {
            EventConfig *event;
            QVector<ModuleConfig *> modules;
            QVector<u8> moduleTypes;
        };

        QVector<EventWithModules> m_events;
};

#endif /* __MVME_MVLC_READOUT_WORKER_H__ */
