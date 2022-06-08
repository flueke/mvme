/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
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
    auto error_logger = [this] (const QString &msg) { getContext().errorLogger(msg); };

    vme_script::run_script_options::Flag opts = 0u;
    if (!m_workerContext.runInfo.ignoreStartupErrors)
        opts = vme_script::run_script_options::AbortOnError;

    auto initResults = vme_daq_init(m_workerContext.vmeConfig, ctrl, logger, error_logger, opts);

    if (!m_workerContext.runInfo.ignoreStartupErrors
        && has_errors(initResults))
    {
        logMessage("");
        logMessage("VME Init Startup Errors:");
        auto logger = [this] (const QString &msg) { logMessage("  " + msg); };
        log_errors(initResults, logger);
        //logMessage("Please make sure the above VME module addresses are correct.");
        return false;
    }

    return true;
}
