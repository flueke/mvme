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

namespace gen_flags
{
    using Flag = u8;
    static const Flag Default = 0u;
    static const Flag MetaIncludeDefaultUnitNames = 1u << 0;
};

QString lookup_name(const TriggerIO &cfg, const UnitAddress &addr);

QString generate_trigger_io_script_text(
    const TriggerIO &ioCfg,
    const gen_flags::Flag &flags = gen_flags::MetaIncludeDefaultUnitNames);

TriggerIO parse_trigger_io_script_text(const QString &text);

} // end namespace mvme_mvlc
} // end namespace mesytec
} // end namespace trigger_io

#endif /* __MVME_MVLC_TRIGGER_IO_SCRIPT_H__ */
