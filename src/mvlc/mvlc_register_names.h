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
#ifndef __MVME_MVLC_REGISTER_NAMES_H__
#define __MVME_MVLC_REGISTER_NAMES_H__

#include <map>
#include <string>

#ifdef QT_CORE_LIB
#include <QMap>
#include <QString>
#endif

#include "typedefs.h"

namespace mesytec
{
namespace mvme_mvlc
{

const std::map<u16, std::string> &get_addr_2_name_map();
const std::map<std::string, u16> &get_name_2_addr_map();

#ifdef QT_CORE_LIB
const QMap<u16, QString> &get_addr_2_name_qmap();
const QMap<QString, u16> &get_name_2_addr_qmap();
#endif

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_REGISTER_NAMES_H__ */
