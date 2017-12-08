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
#ifndef __TREEWIDGET_UTIL_H__
#define __TREEWIDGET_UTIL_H__

#include <QStyledItemDelegate>
#include <QTreeWidgetItem>

// Solution to only allow editing of certain columns while still using the QTreeWidget.
// Source: http://stackoverflow.com/a/4657065
class NoEditDelegate: public QStyledItemDelegate
{
    public:
        NoEditDelegate(QObject* parent=0): QStyledItemDelegate(parent) {}
        virtual QWidget* createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
            return 0;
        }
};

template<typename Pred>
QList<QTreeWidgetItem *> findItems(QTreeWidgetItem *root, Pred predicate)
{
    QList<QTreeWidgetItem *> result;
    findItems(root, predicate, &result);
    return result;
}

template<typename Pred>
void findItems(QTreeWidgetItem *root, Pred predicate, QList<QTreeWidgetItem *> *dest)
{
    if (predicate(root))
        dest->push_back(root);

    for (int childIndex=0; childIndex<root->childCount(); ++childIndex)
    {
        auto child = root->child(childIndex);
        findItems(child, predicate, dest);
    }
}

template<typename Predicate>
QTreeWidgetItem *findFirstNode(QTreeWidgetItem *node, Predicate predicate)
{
    if (predicate(node))
        return node;

    int childCount = node->childCount();

    for (int i = 0; i < childCount; ++i)
    {
        if (auto result = findFirstNode(node->child(i), predicate))
        {
            return result;
        }
    }

    return nullptr;
}

// Item delegate supporting rich text by using a QTextDocument internally.
// Source: http://stackoverflow.com/a/2039745
class HtmlDelegate : public QStyledItemDelegate
{
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

    protected:
        void paint ( QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index ) const;
        QSize sizeHint ( const QStyleOptionViewItem & option, const QModelIndex & index ) const;
};

#endif /* __TREEWIDGET_UTIL_H__ */
