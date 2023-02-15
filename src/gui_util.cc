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
#include "gui_util.h"
#include <QDebug>
#include <QFile>
#include <QHBoxLayout>
#include <QPainter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextStream>
#include <QWidget>

#include "qt_util.h"

#define QSL(str) QStringLiteral(str)

QPixmap embellish_pixmap(const QString &original_source, const QString &embellishment_source)
{
    QPixmap result(original_source);
    QPixmap embellishment(embellishment_source);
    QRect target_rect(result.width() / 2, result.height() / 2, result.width() / 2, result.height() / 2);
    QPainter painter(&result);
    painter.drawPixmap(target_rect, embellishment, embellishment.rect());
    return result;
}

QLabel *make_framed_description_label(const QString &text, QWidget *parent)
{
    auto label = new FixWordWrapBugLabel(text, parent);

    set_widget_font_pointsize_relative(label, -1);

    QSizePolicy pol(label->sizePolicy().horizontalPolicy(),
                    QSizePolicy::Minimum);
    label->setSizePolicy(pol);

    label->setWordWrap(true);
    label->setTextInteractionFlags(label->textInteractionFlags()
                                   | Qt::TextSelectableByMouse);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setFrameShape(QFrame::StyledPanel);

    return label;
}

/* https://bugreports.qt.io/browse/QTBUG-37673 */
void FixWordWrapBugLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent( event );

    if (wordWrap() && sizePolicy().verticalPolicy() == QSizePolicy::Minimum)
    {
        // heightForWidth rely on minimumSize to evaulate, so reset it before
        setMinimumHeight( 0 );
        // define minimum height
        setMinimumHeight( heightForWidth( width() ) );
    }
}

void clear_stacked_widget(QStackedWidget *stackedWidget)
{
    while (auto widget = stackedWidget->currentWidget())
    {
        stackedWidget->removeWidget(widget);
        widget->deleteLater();
    }
    Q_ASSERT(stackedWidget->count() == 0);
}
