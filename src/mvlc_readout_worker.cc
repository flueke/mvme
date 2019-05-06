#include "mvme_mvlc_readout_worker.h"

MVLCReadoutWorker::MVLCReadoutWorker(QObject *parent)
    : VMEReadoutWorker(parent)
{
}

void MVLCReadoutWorker::start(quint32 cycles)
{
}

void MVLCReadoutWorker::stop()
{
}

void MVLCReadoutWorker::pause()
{
}

void MVLCReadoutWorker::resume(quint32 cycles)
{
}

bool MVLCReadoutWorker::isRunning() const
{
}

DAQState MVLCReadoutWorker::getState() const
{
}
