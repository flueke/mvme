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
#ifndef __MVME_MVLC_TRIGGER_IO_SCRIPT_H__
#define __MVME_MVLC_TRIGGER_IO_SCRIPT_H__

#include "mvlc/mvlc_trigger_io.h"

namespace mesytec
{
namespace mvme_mvlc
{
namespace trigger_io
{

static const QString MetaTagMVLCTriggerIO = "mvlc_trigger_io";

// Flags for script text generation from a TriggerIO structure.
namespace gen_flags
{
    using Flag = u8;
    static const Flag Default = 0u;

    // If this flag is set the meta_block and the end of the generated script
    // will contain all unit names even if the name is equal to the default
    // name. Otherwise only modified names will be included in the block.
    static const Flag MetaIncludeDefaultUnitNames = 1u << 0;
};

LIBMVME_EXPORT QString lookup_name(const TriggerIO &cfg, const UnitAddress &addr);

// Generates a VME Script containing all the write commands needed to bring the
// MVLCs Trigger I/O module into the state described by the TriggerIO structure.
LIBMVME_EXPORT QString generate_trigger_io_script_text(
    const TriggerIO &ioCfg,
    const gen_flags::Flag &flags = gen_flags::MetaIncludeDefaultUnitNames);

LIBMVME_EXPORT TriggerIO parse_trigger_io_script_text(const QString &text);

// Loads the default setup from the mvme templates directory.
LIBMVME_EXPORT TriggerIO load_default_trigger_io();

} // end namespace mvme_mvlc
} // end namespace mesytec
} // end namespace trigger_io

#endif /* __MVME_MVLC_TRIGGER_IO_SCRIPT_H__ */
