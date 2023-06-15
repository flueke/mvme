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
#include "data_filter_edit.h"

#include "qt_util.h"
#include "util/qt_font.h"

//
// DataFilterEdit
//

static const int DataFilterMaxBits = 32;

DataFilterEdit::DataFilterEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setAlignment(Qt::AlignRight);

    auto font = make_monospace_font();
    font.setPointSize(9);
    setFont(font);

    updateMaskAndWidth();
    setFilterString(generate_pretty_filter_string(getBitCount(), 'X'));
}

DataFilterEdit::DataFilterEdit(int bits, QWidget *parent)
    : DataFilterEdit(parent)
{
    setBitCount(bits);
    setFilterString(generate_pretty_filter_string(getBitCount(), 'X'));
}

DataFilterEdit::DataFilterEdit(const QString &filterString, QWidget *parent)
    : DataFilterEdit(parent)
{
    setFilterString(filterString);
}

DataFilterEdit::DataFilterEdit(const DataFilter &filter, QWidget *parent)
    : DataFilterEdit(parent)
{
    setFilter(filter);
}

void DataFilterEdit::setBitCount(int bits)
{
    if (bits > DataFilterMaxBits)
        throw std::length_error("maximum filter size of 32 exceeded");

    m_bits = bits;

    updateMaskAndWidth();
}

DataFilter DataFilterEdit::getFilter() const
{
    return makeFilterFromString(text());
}

void DataFilterEdit::setFilter(const DataFilter &filter)
{
    //qDebug() << __PRETTY_FUNCTION__;
    auto tmpText = QString::fromStdString(to_string(filter));

    //qDebug() << "  umodified filter text: " << tmpText;
    //qDebug() << "  this bitCount: " << getBitCount();

    tmpText.replace(" ", "");

    tmpText = tmpText.right(getBitCount());

    while (tmpText.size() < getBitCount())
    {
        tmpText.prepend('X');
    }

    //qDebug() << "  adjusted filter text: " << tmpText;

    setText(tmpText);
}

void DataFilterEdit::setFilterString(const QString &filterString)
{
    setFilter(makeFilterFromString(filterString));
}

QString DataFilterEdit::getFilterString() const
{
    return text();
}

void DataFilterEdit::updateMaskAndWidth()
{
    //qDebug() << __PRETTY_FUNCTION__;

    auto tmpText = text();
    auto mask = generate_pretty_filter_string(getBitCount(), 'N');

    //qDebug() << "  text before mask change: " << tmpText;
    //qDebug() << "  input mask string: " << mask;

    setInputMask(mask);

    if (mask.isEmpty())
        setMaxLength(0);
    else
        setMaxLength(32768);

    //qDebug() << "  text after mask change: " << text();

    tmpText.replace(" ", "");
    tmpText = tmpText.right(getBitCount());

    while (tmpText.size() < getBitCount())
    {
        tmpText.prepend('X');
    }

    //qDebug() << "  new, adjusted text: " << tmpText;

    setText(tmpText);

    QFontMetrics fm(font());
    s32 padding = 6;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    s32 width = fm.horizontalAdvance(inputMask()) + padding;
#else
    s32 width = fm.width(inputMask()) + padding;
#endif
    setMinimumWidth(width);
}

DataFilterEdit *makeFilterEdit(QWidget *parent)
{
#if 1
    return new DataFilterEdit(parent);
#else
    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(9);

    auto result = new DataFilterEdit(parent);
    result->setFont(font);
    result->setInputMask(generate_pretty_filter_string(bits, 'N'));
    result->setAlignment(Qt::AlignRight);

    QFontMetrics fm(font);
    s32 padding = 6;
    s32 width = fm.width(result->inputMask()) + padding;
    result->setMinimumWidth(width);

    return result;
#endif
}
