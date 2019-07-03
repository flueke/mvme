#include "vme_readout_worker.h"
#include "vme_daq.h"

VMEReadoutWorker::VMEReadoutWorker(QObject *parent)
    : QObject(parent)
{
}

bool VMEReadoutWorker::logMessage(const QString &msg, bool useThrottle)
{
    return getContext().logMessage(msg, useThrottle);
}

// Returns false if startup errors are _not_ ignored and the vme_daq_init()
// call yielded an error. In this case the errors are also logged separately.
// Otherwise true is returned.
bool VMEReadoutWorker::do_VME_DAQ_Init(VMEController *ctrl)
{
    auto logger = [this] (const QString &msg) { logMessage(msg); };
    auto initResults = vme_daq_init(m_workerContext.vmeConfig, ctrl, logger);

    if (!m_workerContext.runInfo->ignoreStartupErrors
        && has_errors(initResults))
    {
        logMessage("");
        logMessage("VME Init Startup Errors:");
        auto logger = [this] (const QString &msg) { logMessage("  " + msg); };
        log_errors(initResults, logger);
        logMessage("Please make sure the above VME module addresses are correct.");
        return false;
    }

    return true;
}
