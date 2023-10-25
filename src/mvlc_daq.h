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
#ifndef __MVME_MVLC_DAQ_H__
#define __MVME_MVLC_DAQ_H__

#include "libmvme_export.h"
#include <mesytec-mvlc/mvlc_command_builders.h>
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_trigger_io.h"
#include "vme_config.h"

namespace mesytec
{
namespace mvme_mvlc
{

class MVLC_VMEController;

using Logger = std::function<void (const QString &)>;

std::pair<std::vector<u32>, std::error_code> LIBMVME_EXPORT get_trigger_values(
    const VMEConfig &vmeConfig, Logger logger = [] (const QString &) {});

std::error_code LIBMVME_EXPORT
    enable_triggers(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger);

std::error_code LIBMVME_EXPORT
    disable_all_triggers_and_daq_mode(MVLCObject &mvlc);

std::error_code LIBMVME_EXPORT
    reset_stack_offsets(MVLCObject &mvlc);

mvlc::StackCommandBuilder LIBMVME_EXPORT get_readout_commands(const EventConfig &event);
std::vector<mvlc::StackCommandBuilder> LIBMVME_EXPORT get_readout_stacks(const VMEConfig &vmeConfig);

std::error_code LIBMVME_EXPORT
    setup_readout_stacks(MVLCObject &mvlc, const VMEConfig &vmeConfig, Logger logger);

std::error_code LIBMVME_EXPORT
    setup_trigger_io(MVLC_VMEController *mvlc, VMEConfig &vmeConfig, Logger logger);

// Parses the trigger io contained in the vmeconfig, updates it to handle
// periodic and externally triggered events and returns the updated TriggerIO
// structure.
mesytec::mvme_mvlc::trigger_io::TriggerIO LIBMVME_EXPORT
    update_trigger_io(const VMEConfig &vmeConfig);

// Reads the trigger io script from the VMEConfig, runs update_trigger_io(),
// recreates the script and stores it back in the VMEConfig.
void LIBMVME_EXPORT update_trigger_io_inplace(const VMEConfig &vmeConfig);

mvlc::StackCommandBuilder LIBMVME_EXPORT make_module_init_stack(const VMEConfig &vmeConfig);

// Removes non-output-producing command groups from each of the readout
// stacks. This is done because the converted CrateConfig contains
// groups for the "Cycle Start" and "Cycle End" event scripts which do
// not produce any output. Having a Cycle Start script (called
// "readout_start" in the CrateConfig) will confuse the readout parser
// because the readout stack group indexes and the mvme module indexes
// won't match up.
std::vector<mvlc::StackCommandBuilder> LIBMVME_EXPORT sanitize_readout_stacks(
    const std::vector<mvlc::StackCommandBuilder> &inputStacks);

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_DAQ_H__ */
