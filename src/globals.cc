/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "globals.h"

QString toString(const ListFileFormat &fmt)
{
    switch (fmt)
    {
        case ListFileFormat::Invalid:
            return QSL("Invalid");
        case ListFileFormat::Plain:
            return QSL("Plain");
        case ListFileFormat::ZIP:
            return QSL("ZIP");
    }

    return QString();
}

ListFileFormat fromString(const QString &str)
{
    if (str == "Plain")
        return ListFileFormat::Plain;

    if (str == "ZIP")
        return ListFileFormat::ZIP;

    return ListFileFormat::Invalid;
}

QString generate_output_basename(const ListFileOutputInfo &info)
{
    QString result(info.prefix);

    if (info.flags & ListFileOutputInfo::UseRunNumber)
    {
        result += QString("_%1").arg(info.runNumber, 3, 10, QLatin1Char('0'));
    }

    if (info.flags & ListFileOutputInfo::UseTimestamp)
    {
        auto now = QDateTime::currentDateTime();
        result += QSL("_") + now.toString("yyMMdd_HHmmss");
    }

    return result;
}

QString generate_output_filename(const ListFileOutputInfo &info)
{
    QString result = generate_output_basename(info);

    switch (info.format)
    {
        case ListFileFormat::Plain:
            result += QSL(".mvmelst");
            break;

        case ListFileFormat::ZIP:
            result += QSL(".zip");
            break;

        InvalidDefaultCase;
    }

    return result;
}
