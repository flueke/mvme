/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "gui_util.h"
#include <QDebug>
#include <QFile>
#include <QHBoxLayout>
#include <QPainter>
#include <QTextBrowser>
#include <QTextStream>
#include <QWidget>

#define QSL(str) QStringLiteral(str)

QWidget *make_vme_script_ref_widget()
{
    QWidget *widget = nullptr;

    QFile inFile(":/vme-script-help.html");
    if (inFile.open(QIODevice::ReadOnly))
    {
        auto tb = new QTextBrowser;
        QTextStream inStream(&inFile);
        tb->document()->setHtml(inStream.readAll());

        // scroll to top
        auto cursor = tb->textCursor();
        cursor.setPosition(0);
        tb->setTextCursor(cursor);

        widget = new QWidget;
        widget->setObjectName("VMEScriptReference");
        widget->setWindowTitle(QSL("VME Script Reference"));
        auto layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(tb);
    }

    return widget;
}

QPixmap embellish_pixmap(const QString &original_source, const QString &embellishment_source)
{
    QPixmap result(original_source);
    QPixmap embellishment(embellishment_source);
    QRect target_rect(result.width() / 2, result.height() / 2, result.width() / 2, result.height() / 2);
    QPainter painter(&result);
    painter.drawPixmap(target_rect, embellishment, embellishment.rect());
    return result;
}
