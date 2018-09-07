/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "treewidget_utils.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QPainter>
#include <QTextDocument>
#include <QDebug>

#include "util.h"

void HtmlDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    QStyleOptionViewItem optionV4 = option;
    initStyleOption(&optionV4, index);

    QStyle *style = optionV4.widget? optionV4.widget->style() : QApplication::style();

    QTextDocument doc;
    doc.setDefaultFont(optionV4.font);
    doc.setHtml(optionV4.text);
    doc.setDocumentMargin(1);

    /// Painting item without text
    optionV4.text = QString();
    style->drawControl(QStyle::CE_ItemViewItem, &optionV4, painter);

    QAbstractTextDocumentLayout::PaintContext ctx;

    // Highlighting text if item is selected
    if (optionV4.state & QStyle::State_Selected)
    {
        ctx.palette.setColor(QPalette::Text,
                             optionV4.palette.color(QPalette::Active,
                                                    QPalette::HighlightedText));
    }

    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &optionV4);
    painter->save();
    painter->translate(textRect.topLeft());
    painter->setClipRect(textRect.translated(-textRect.topLeft()));
    doc.documentLayout()->draw(painter, ctx);
    painter->restore();
}

QSize HtmlDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem optionV4 = option;
    initStyleOption(&optionV4, index);

    QTextDocument doc;
    doc.setDefaultFont(optionV4.font);
    doc.setHtml(optionV4.text);
    doc.setDocumentMargin(1);
    doc.setTextWidth(optionV4.rect.width());
    return QSize(doc.idealWidth(), doc.size().height());
}

void CanDisableItemsHtmlDelegate::initStyleOption(QStyleOptionViewItem *option,
                                                  const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);

    if (auto node = reinterpret_cast<QTreeWidgetItem *>(index.internalPointer()))
    {
        if (m_isItemDisabled && m_isItemDisabled(node))
        {
            option->state &= ~QStyle::State_Enabled;
        }
    }
}

void BasicTreeNode::setData(int column, int role, const QVariant &value)
{
    if (column < 0)
        return;

    if (role != Qt::DisplayRole && role != Qt::EditRole)
    {
        QTreeWidgetItem::setData(column, role, value);
        return;
    }

    if (column >= m_columnData.size())
    {
        m_columnData.resize(column + 1);
    }

    auto &entry = m_columnData[column];

    switch (role)
    {
        case Qt::DisplayRole:
            if (entry.displayData != value)
            {
                entry.displayData = value;
                entry.flags |= Data::HasDisplayData;
                emitDataChanged();
            }
            break;

        case Qt::EditRole:
            if (entry.editData != value)
            {
                entry.editData = value;
                entry.flags |= Data::HasEditData;
                emitDataChanged();
            }
            break;

            InvalidDefaultCase;
    }
}

QVariant BasicTreeNode::data(int column, int role) const
{
    if (role != Qt::DisplayRole && role != Qt::EditRole)
    {
        return QTreeWidgetItem::data(column, role);
    }

    if (0 <= column && column < m_columnData.size())
    {
        const auto &entry = m_columnData[column];

        switch (role)
        {
            case Qt::DisplayRole:
                if (entry.flags & Data::HasDisplayData)
                    return entry.displayData;
                return entry.editData;

            case Qt::EditRole:
                if (entry.flags & Data::HasEditData)
                    return entry.editData;
                return entry.displayData;

                InvalidDefaultCase;
        }
    }

    return QVariant();
}
