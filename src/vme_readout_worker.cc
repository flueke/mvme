#include "vme_readout_worker.h"

VMEReadoutWorker::VMEReadoutWorker(QObject *parent)
    : QObject(parent)
{
}

void VMEReadoutWorker::logMessage(const QString &msg, bool useThrottle)
{
    getContext().logMessage(msg, useThrottle);
}
