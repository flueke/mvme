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
#include "histo_gui_util.h"

#include <QPen>

#include "typedefs.h"
#include "util/qt_str.h"

static const u32 RRFMin = 2;

QSlider *make_res_reduction_slider(QWidget *parent)
{
    auto result = new QSlider(Qt::Horizontal, parent);

    result->setSingleStep(1);
    result->setPageStep(1);
    result->setMinimum(RRFMin);

    return result;
}

std::unique_ptr<QComboBox> make_res_selection_combo()
{
    auto ret = std::make_unique<QComboBox>();

    for (u32 bits=2; bits<=16; ++bits)
    {
        u32 value = 1u << bits;
        auto text = QSL("%1 (%2 bit)").arg(value).arg(bits);
        ret->addItem(text, value);
    }

    return ret;
}

std::unique_ptr<QwtText> make_qwt_text_box(int renderFlags, int fontPixelSize)
{
    auto result = std::make_unique<QwtText>();

    /* This controls the alignment of the whole text on the canvas aswell as
     * the alignment of the text itself. */
    result->setRenderFlags(renderFlags);

    QPen borderPen(Qt::SolidLine);
    borderPen.setColor(Qt::black);
    result->setBorderPen(borderPen);

    QBrush brush;
    brush.setColor("#e6e2de");
    brush.setStyle(Qt::SolidPattern);
    result->setBackgroundBrush(brush);

    /* The text rendered by qwt looked non-antialiased when using the RichText
     * format. Manually setting the pixelSize fixes this. */
    QFont font;
    font.setPixelSize(fontPixelSize);
    result->setFont(font);

    return result;
}
