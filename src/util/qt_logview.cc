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
#include "util/qt_logview.h"
#include "util/qt_monospace_textedit.h"
#include <QMenu>

std::unique_ptr<QPlainTextEdit> make_logview(size_t maxBlockCount)
{
    auto result = mesytec::mvme::util::make_monospace_plain_textedit();

    result->setAttribute(Qt::WA_DeleteOnClose);
    result->setReadOnly(true);
    result->setWindowTitle("Log View");
    result->setTabChangesFocus(true);
    result->document()->setMaximumBlockCount(maxBlockCount);
    result->setContextMenuPolicy(Qt::CustomContextMenu);
    result->setStyleSheet("background-color: rgb(225, 225, 225);");

    auto raw = result.get();

    QObject::connect(
        raw, &QWidget::customContextMenuRequested,
        raw, [=](const QPoint &pos)
    {
        auto menu = raw->createStandardContextMenu(pos);
        auto action = menu->addAction("Clear");
        QObject::connect(action, &QAction::triggered, raw, &QPlainTextEdit::clear);
        menu->exec(raw->mapToGlobal(pos));
        menu->deleteLater();
    });

    return result;
}
