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
#ifndef __MVME_UTIL_QT_MONOSPACE_TEXTEDIT_H__
#define __MVME_UTIL_QT_MONOSPACE_TEXTEDIT_H__

#include <memory>
#include <QtGlobal>

#include "libmvme_export.h"
#include "util/qt_font.h"

class QFont;
class QPlainTextEdit;
class QTextEdit;

namespace mesytec
{
namespace mvme
{
namespace util
{

// These functions create a QPlainTextEdit or a QTextEdit respectively. The
// textedit is setup to use a monospace font, a tabstop width of 4 characters
// and it doesn't allow to wrap long lines.
// The pointSizeDelta argument can be used to add/subtract to/from the default
// font point size.
// Alternatively the font to use can be dircetly passed in.

std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_monospace_plain_textedit();
std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_monospace_plain_textedit(qreal pointSizeDelta);
std::unique_ptr<QPlainTextEdit> LIBMVME_EXPORT make_monospace_plain_textedit(const QFont &font);

std::unique_ptr<QTextEdit> LIBMVME_EXPORT make_monospace_textedit();
std::unique_ptr<QTextEdit> LIBMVME_EXPORT make_monospace_textedit(qreal pointSizeDelta);
std::unique_ptr<QTextEdit> LIBMVME_EXPORT make_monospace_textedit(const QFont &font);

namespace plain_textedit_detail
{
    template<typename T>
        std::unique_ptr<T> impl(const QFont &font)
        {
            auto result = std::make_unique<T>();
            result->setFont(font);
            result->setLineWrapMode(T::NoWrap);
            set_tabstop_width(result.get(), 4);

            return result;
        }

    template<typename T>
        std::unique_ptr<T> impl(qreal pointSizeDelta)
        {
            auto font = make_monospace_font();
            font.setPointSizeF(font.pointSizeF() + pointSizeDelta);

            return impl<T>(font);
        }
}

} // end namespace util
} // end namespace mvme
} // end namespace mesytec

#endif /* __MVME_UTIL_QT_MONOSPACE_TEXTEDIT_H__ */
