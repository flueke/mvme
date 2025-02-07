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
#include "util/qt_monospace_textedit.h"

#include "util/qt_font.h"

namespace
{

} // namespace

namespace mesytec::mvme::util
{

std::unique_ptr<QPlainTextEdit> make_monospace_plain_textedit()
{
    return plain_textedit_detail::impl<QPlainTextEdit>();
}

std::unique_ptr<QPlainTextEdit> make_plain_textedit(const QFont &font)
{
    return plain_textedit_detail::impl<QPlainTextEdit>(font);
}

std::unique_ptr<QTextEdit> make_monospace_textedit()
{
    return plain_textedit_detail::impl<QTextEdit>();
}

std::unique_ptr<QTextEdit> make_textedit(const QFont &font)
{
    return plain_textedit_detail::impl<QTextEdit>(font);
}

} // namespace mesytec::mvme::util
