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
#include "util/qt_logview.h"

#include <QMenu>
#include <QDebug>
#include <unordered_map>
#include <QBoxLayout>

#include "util/qt_monospace_textedit.h"
#include "util/qt_plaintextedit.h"
#include "util/qt_str.h"

std::unique_ptr<QPlainTextEdit> make_logview(size_t maxBlockCount)
{
    using namespace mesytec::mvme::util;

    auto result = mesytec::mvme::util::plain_textedit_detail::impl<PlainTextEdit>();

    result->setAttribute(Qt::WA_DeleteOnClose);
    result->setReadOnly(true);
    result->setWindowTitle("Log View");
    result->setTabChangesFocus(true);
    result->document()->setMaximumBlockCount(maxBlockCount);
    result->setContextMenuPolicy(Qt::CustomContextMenu);
    result->setStyleSheet("background-color: rgb(225, 225, 225);");
    result->setTextInteractionFlags(result->textInteractionFlags() | Qt::LinksAccessibleByMouse);

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

    QObject::connect(
        raw, &PlainTextEdit::linkActivated,
        raw, [] (const QString &link)
        {
            qDebug() << __PRETTY_FUNCTION__ << "link activated: " << link;
        });

    return result;
}

struct MultiLogWidget::Private
{
    std::unordered_map<QString, QPlainTextEdit> logViews;
    // TODO: add a tab widget and multiple log views
    QPlainTextEdit *logView;
};

MultiLogWidget::MultiLogWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->logView = make_logview().release();

    auto layout = new QHBoxLayout(this);
    layout->addWidget(d->logView);
}

MultiLogWidget::~MultiLogWidget()
{
}

void MultiLogWidget::logMessage(const QString &msg)
{
    auto escaped = msg.toHtmlEscaped();
    auto html = QSL("<font color=\"black\"><pre>%1</pre></font>").arg(escaped);
    d->logView->appendHtml(html);
}

void MultiLogWidget::logMessage(const QString &category, const QString &msg)
{
    auto str = QSL("[%1] %2").arg(category).arg(msg);
    auto escaped = str.toHtmlEscaped();
    auto html = QSL("<font color=\"black\"><pre>%1</pre></font>").arg(escaped);
    d->logView->appendHtml(html);
}
