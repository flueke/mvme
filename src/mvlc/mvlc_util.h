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
#ifndef __MVME_MVLC_UTIL_H__
#define __MVME_MVLC_UTIL_H__

#include <iomanip>
#include "libmvme_mvlc_export.h"
#include <mesytec-mvlc/mvlc_constants.h>
#include "vme_script.h"

namespace mesytec
{
namespace mvme_mvlc
{

// vme_script -> mvlc constant
LIBMVME_MVLC_EXPORT mvlc::VMEDataWidth convert_data_width(vme_script::DataWidth width);
LIBMVME_MVLC_EXPORT u8 convert_data_width_untyped(vme_script::DataWidth width);

// mvlc constant -> vme_script
LIBMVME_MVLC_EXPORT vme_script::DataWidth convert_data_width(mvlc::VMEDataWidth dataWidth);

// Returns the raw stack without any interleaved super (upload) commands.
// The stack result will be written to the given output pipe.
LIBMVME_MVLC_EXPORT std::vector<u32> build_stack(
    const vme_script::VMEScript &script, u8 outPipe);

// Returns a Command Buffer List which writes the contents of the given stack
// or VMEScript to the MVLC stack memory area.
LIBMVME_MVLC_EXPORT std::vector<u32> build_upload_commands(
    const vme_script::VMEScript &script,
    u8 outPipe,
    u16 startAddress);

LIBMVME_MVLC_EXPORT std::vector<u32> build_upload_commands(
    const std::vector<u32> &stack,
    u16 startAddress);

// Same as build_upload_commands but the returned list will be enclosed in
// CmdBufferStart and CmdBufferEnd. This is a form that can be parsed by the MVLC.
LIBMVME_MVLC_EXPORT std::vector<u32> build_upload_command_buffer(
    const vme_script::VMEScript &script, u8 outPipe,
    u16 startAddress);

// Same as build_upload_command_buffer but instead of taking a VMEScript to
// build the stack data from this overload takes in raw (stack) data to be
// uploaded.
LIBMVME_MVLC_EXPORT std::vector<u32> build_upload_command_buffer(
    const std::vector<u32> &stack, u16 startAddress);

LIBMVME_MVLC_EXPORT void log_buffer(const QVector<u32> &buffer, const QString &info = {});

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVLC_UTIL_H__ */
