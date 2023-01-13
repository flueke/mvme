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
#include "strings.h"
#include "typedefs.h"
#include <QStringList>

#ifndef QSL
#define QSL(str) QStringLiteral(str)
#endif

static const QStringList PositivePrefixes =
{
    QSL(""),
    QSL("k"), // kilo
    QSL("M"), // mega
    QSL("G"), // giga
    QSL("T"), // tera
    QSL("P"), // peta
    QSL("E"), // exa
};

#if 0
static const QStringList NegativePrefixes =
{
    QSL(""),
    QSL("m"), // milli
    QSL("µ"), // micro
    QSL("n"), // nano
    QSL("p"), // pico
    QSL("f"), // femto
    QSL("a"), // atto
};
#endif

QString format_number(double value, const QString &unit,  UnitScaling scaling,
                      int fieldWidth, char format, int precision, QChar fillChar)
{
    const double factor = (scaling == UnitScaling::Binary) ? 1024.0 : 1000.0;

    s32 prefixIndex = 0;

    if (value >= 0)
    {
        while (value >= factor)
        {
            value /= factor;
            prefixIndex++;

            if (prefixIndex == PositivePrefixes.size() - 1)
                break;
        }
    }

    QString result(QSL("%1 %2%3")
                   .arg(value, fieldWidth, format, precision, fillChar)
                   .arg(PositivePrefixes[prefixIndex])
                   .arg(unit)
                  );

    return result;
}
