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
#include "util/variablify.h"
#include <QRegularExpression>

QString variablify(QString str)
{
    QRegularExpression ReIsValidFirstChar("[a-zA-Z_]");
    QRegularExpression ReIsValidChar("[a-zA-Z0-9_]");

    for (int i = 0; i < str.size(); i++)
    {
        QRegularExpressionMatch match;

        if (i == 0)
        {
            match = ReIsValidFirstChar.match(str, i, QRegularExpression::NormalMatch,
                                             QRegularExpression::AnchoredMatchOption);
        }
        else
        {
            match = ReIsValidChar.match(str, i, QRegularExpression::NormalMatch,
                                        QRegularExpression::AnchoredMatchOption);
        }

        if (!match.hasMatch())
        {
            //qDebug() << "re did not match on" << str.mid(i);
            //qDebug() << "replacing " << str[i] << " with _ in " << str;
            str[i] = '_';
        }
        else
        {
            //qDebug() << "re matched: " << match.captured(0);
        }
    }

    return str;
}

