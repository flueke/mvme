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
#ifndef __MVME_UTIL_QT_FONT_H__
#define __MVME_UTIL_QT_FONT_H__

#include <cmath>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QString>
#include <QTextEdit>

inline qreal calculate_tabstop_width(const QFont &font, int tabstop)
{
    QFontMetricsF metrics(font);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    auto stopWidth = tabstop * metrics.horizontalAdvance(' ');
#else
    auto stopWidth = tabstop * metrics.width(' ');
#endif

    return stopWidth;
}

template<typename TextEdit>
inline void set_tabstop_width(TextEdit *textEdit, int tabstop)
{
    auto pixelWidth = calculate_tabstop_width(textEdit->font(), tabstop);

#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    textEdit->setTabStopDistance(pixelWidth);
#else
    textEdit->setTabStopWidth(std::ceil(pixelWidth));
#endif
}

inline QFont make_monospace_font()
{
    QFont baseFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    baseFont.setStyleHint(QFont::Monospace);
    baseFont.setFixedPitch(true);
    return baseFont;
}

#endif /* __QT_FONT_H__ */
