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
#include <mesytec-mvlc/mesytec-mvlc.h>
#include "vme_script.h"

namespace mesytec
{
namespace mvme_mvlc
{

LIBMVME_MVLC_EXPORT mvlc::StackCommandBuilder
    build_mvlc_stack(const vme_script::VMEScript &script);

LIBMVME_MVLC_EXPORT mvlc::StackCommandBuilder
    build_mvlc_stack(const std::vector<vme_script::Command> &script);

LIBMVME_MVLC_EXPORT void log_buffer(const QVector<u32> &buffer, const QString &info = {});

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVLC_UTIL_H__ */
