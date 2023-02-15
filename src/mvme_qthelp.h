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
#ifndef __MVME_QTHELP_H__
#define __MVME_QTHELP_H__

#include "qt_assistant_remote_control.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QString>

namespace mesytec
{
namespace mvme
{

QString get_mvme_qthelp_index_url();

inline auto make_help_keyword_handler(QDialogButtonBox *bb, const QString &keyword)
{
    auto handler = [bb, keyword] (QAbstractButton *button)
    {
        if (button == bb->button(QDialogButtonBox::Help))
            mvme::QtAssistantRemoteControl::instance().activateKeyword(keyword);
    };

    return handler;
}

inline auto make_help_keyword_handler(const QString &keyword)
{
    auto handler = [keyword] ()
    {
        mvme::QtAssistantRemoteControl::instance().activateKeyword(keyword);
    };

    return handler;
}

}
}

#endif /* __MVME_QTHELP_H__ */
