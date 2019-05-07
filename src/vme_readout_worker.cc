#include "vme_readout_worker.h"

VMEReadoutWorker::VMEReadoutWorker(QObject *parent)
    : QObject(parent)
{
}

bool VMEReadoutWorker::logMessage(const QString &msg, bool useThrottle)
{
    return getContext().logMessage(msg, useThrottle);
}
