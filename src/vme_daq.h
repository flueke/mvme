/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __VME_DAQ_H__
#define __VME_DAQ_H__

#include "libmvme_export.h"

#include "vme_config.h"
#include "vme_controller.h"
#include "vme_readout_worker.h"
#include "vme_script.h"
#include "vme_script_exec.h"

/* Both init functions throw on error:
 * QString, std::runtime_error, vme_script::ParseError
 */

/* Runs the following vme scripts from the vme configuration using the given
 * vme controller:
 * - global DAQ start scripts
 * - for each event:
 *     - for each module:
 *       - module reset script
 *       - module init scripts
 * - for each event:
 *     - event DAQ start script
 */

struct ScriptWithResults
{
    // Non-owning pointer to the vme script config that produced the result
    // list.
    const VMEScriptConfig *scriptConfig = nullptr;

    // The symbol tables used when evaluating the script.
    //const vme_script::SymbolTables symbols;

    // List of results of running the script.
    vme_script::ResultList results;

    // A ParseError instance or nullptr if parsing the script was sucessful.
    std::shared_ptr<vme_script::ParseError> parseError = {};
};

QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_run_global_daq_start_scripts(
    const VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0);

QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_run_global_daq_stop_scripts(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0);

QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_run_init_modules(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0);

QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_run_event_daq_start_scripts(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0);

QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_run_event_daq_stop_scripts(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0);


// Standard VME init sequence for VMUSB and SIS3153. MVLC does its own thing
// since 1.4.0-beta.
QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_init(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    vme_script::run_script_options::Flag opts = 0u);

QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_init(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0u);

/* Counterpart to vme_daq_init for the VMUSB and SIS3153.
 * Runs
 * - for each event
 *     - event DAQ stop script
 * - global DAQ stop scripts
 */
QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_shutdown(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    vme_script::run_script_options::Flag opts = 0);

QVector<ScriptWithResults> LIBMVME_EXPORT
vme_daq_shutdown(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0);

// Since 1.4.0-beta the mvlc runs the mcast event daq stop scripts in the
// readout worker. This version of the shutdown function only runs the global
// daq stop scripts.
QVector<ScriptWithResults> LIBMVME_EXPORT
mvlc_daq_shutdown(
    VMEConfig *vmeConfig,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts = 0);

bool LIBMVME_EXPORT has_errors(const QVector<ScriptWithResults> &results);

void LIBMVME_EXPORT log_errors(const QVector<ScriptWithResults> &results,
                               std::function<void (const QString &)> logger);


struct LIBMVME_EXPORT EventReadoutBuildFlags
{
    static const u8 None = 0u;
    static const u8 NoModuleEndMarker = 1u;
};

/* Builds a vme script containing the readout commands for the given event:
 * - event readout start ("cycle start" in the GUI)
 * - for each module:
 *     - module readout script (empty if module is disabled)
 *     - EndMarker command (if NoModuleEndMarker is not specified in the given flags)
 * - event readout end ("cycle end" in the GUI)
 */
vme_script::VMEScript LIBMVME_EXPORT build_event_readout_script(
    const EventConfig *eventConfig,
    u8 flags = EventReadoutBuildFlags::None);

struct DAQReadoutListfileHelperPrivate;

// Helper class for the old mvmelst custom format. Used by the SIS3153 and VMUSB readout workers.
class LIBMVME_EXPORT DAQReadoutListfileHelper
{
    public:
        explicit DAQReadoutListfileHelper(VMEReadoutWorkerContext &readoutContext);
        ~DAQReadoutListfileHelper();

        void beginRun();
        void endRun();
        void writeBuffer(DataBuffer *buffer);
        void writeBuffer(const u8 *buffer, size_t size);
        void writeTimetickSection();
        void writePauseSection();
        void writeResumeSection();

    private:
        std::unique_ptr<DAQReadoutListfileHelperPrivate> m_d;
        VMEReadoutWorkerContext &m_readoutContext;
};

/* Throws if neither UseRunNumber nor UseTimestamp is set and the file already
 * exists. Otherwise tries until it hits a non-existant filename. In the odd
 * case where a timestamped filename exists and only UseTimestamp is set this
 * process will take 1s!
 *
 * Also note that the file handling code does not in any way guard against race
 * conditions when someone else is also creating files.
 *
 * Note: Increments the runNumber of outInfo if UseRunNumber is set in the
 * output flags.
 */
QString LIBMVME_EXPORT make_new_listfile_name(ListFileOutputInfo *outInfo);

// global daq start scripts
QVector<VMEScriptConfig *> LIBMVME_EXPORT collect_global_daq_start_scripts(const VMEConfig *vmeConfig);

// global daq stop scripts
QVector<VMEScriptConfig *> LIBMVME_EXPORT collect_global_daq_stop_scripts(const VMEConfig *vmeConfig);

// module init scripts
QVector<VMEScriptConfig *> LIBMVME_EXPORT collect_module_daq_start_scripts(const VMEConfig *vmeConfig);

// mcst daq start scripts for all events
QVector<VMEScriptConfig *> LIBMVME_EXPORT collect_event_mcst_daq_start_scripts(const VMEConfig *vmeConfig);

// mcst daq stop scripts for all events
QVector<VMEScriptConfig *> LIBMVME_EXPORT collect_event_mcst_daq_stop_scripts(const VMEConfig *vmeConfig);

// Returns the readout scripts for the given EventConfig. The first script is
// the event-wide "readout_start" script, the last one the event-widget
// "readout_stop" script.
QVector<VMEScriptConfig *> LIBMVME_EXPORT collect_module_readout_scripts(const EventConfig *ev, bool includeDisabledModules = false);

// Returns the readout scripts for each event, e.g. [1] contains the module
// readout scripts for event=1
QVector<QVector<VMEScriptConfig *>> LIBMVME_EXPORT collect_module_readout_scripts(const VMEConfig *vmeConfig);

#endif /* __VME_DAQ_H__ */
