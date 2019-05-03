#ifndef __MVME_MVLC_READOUT_WORKER_H__
#define __MVME_MVLC_READOUT_WORKER_H__

#include "vme_readout_worker.h"

class MVLCReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    public:
        MVLCReadoutWorker(QObject *parent = nullptr);

        void start(quint32 cycles = 0) override;
        void stop() override;
        void pause() override;
        void resume(quint32 cycles = 0) override;
        bool isRunning() const override;
        DAQState getState() const override;
};

#endif /* __MVME_MVLC_READOUT_WORKER_H__ */
