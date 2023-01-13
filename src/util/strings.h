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
#ifndef __MVME_UTIL_STRINGS_H__
#define __MVME_UTIL_STRINGS_H__

#include "libmvme_export.h"

#include <QString>
#include "typedefs.h"

enum class UnitScaling
{
    Binary,
    Decimal,
};

QString
LIBMVME_EXPORT format_number(
    double value, const QString &unit,  UnitScaling scaling,
    int fieldWidth = 0, char format = 'g', int precision = -1,
    QChar fillChar = QLatin1Char(' '));

inline QString format_ipv4(u32 address)
{
    return QString("%1.%2.%3.%4")
        .arg((address >> 24) & 0xFF)
        .arg((address >> 16) & 0xFF)
        .arg((address >>  8) & 0xFF)
        .arg((address >>  0) & 0xFF);
}

#endif /* __MVME_UTIL_STRINGS_H__ */
