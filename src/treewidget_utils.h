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
#ifndef __TREEWIDGET_UTIL_H__
#define __TREEWIDGET_UTIL_H__

#include <functional>
#include <memory>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>

#include "typedefs.h"

// Solution to only allow editing of certain columns while still using the QTreeWidget.
// Source: http://stackoverflow.com/a/4657065
class NoEditDelegate: public QStyledItemDelegate
{
    public:
        explicit NoEditDelegate(QObject* parent=0): QStyledItemDelegate(parent) {}

        virtual QWidget* createEditor(QWidget *parent,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const
        {
            (void) parent;
            (void) option;
            (void) index;
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
        explicit HtmlDelegate(QObject *parent = nullptr);
        virtual ~HtmlDelegate() override;

    protected:
        void paint(QPainter *painter,
                   const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

        QSize sizeHint(const QStyleOptionViewItem &option,
                       const QModelIndex &index) const override;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

class CanDisableItemsHtmlDelegate: public HtmlDelegate
{
    public:
        using IsItemDisabledFunctor = std::function<bool (QTreeWidgetItem *)>;

        CanDisableItemsHtmlDelegate(const IsItemDisabledFunctor &isItemDisabled,
                                    QObject *parent = nullptr)
            : HtmlDelegate(parent)
            , m_isItemDisabled(isItemDisabled)
        {}

    protected:
        virtual void initStyleOption(QStyleOptionViewItem *option,
                                     const QModelIndex &index) const override;

    private:
        IsItemDisabledFunctor m_isItemDisabled;
};

/* QTreeWidgetItem does not support setting separate values for Qt::DisplayRole and
 * Qt::EditRole. This subclass removes this limitation.
 *
 * The implementation keeps track of whether DisplayRole and EditRole data have been set.
 * If specific data for the requested role is available it will be returned, otherwise the
 * other roles data is returned.
 *
 * NOTE: Do not use for the headerview as that requires special handling which needs
 * access to the private QTreeModel class.
 * Link to the Qt code: https://code.woboq.org/qt5/qtbase/src/widgets/itemviews/qtreewidget.cpp.html#_ZN15QTreeWidgetItem7setDataEiiRK8QVariant
 */
class BasicTreeNode: public QTreeWidgetItem
{
    public:
        explicit BasicTreeNode(int type = QTreeWidgetItem::Type)
            : QTreeWidgetItem(type)
        { }

        BasicTreeNode(const QStringList &strings, int type = QTreeWidgetItem::Type)
            : QTreeWidgetItem(type)
        {
            for (int i = 0; i < strings.size(); i++)
            {
                setText(i, strings.at(i));
            }
        }

        virtual void setData(int column, int role, const QVariant &value) override;
        virtual QVariant data(int column, int role) const override;

    private:
        struct Data
        {
            static const u8 HasDisplayData = 1u << 0;
            static const u8 HasEditData    = 1u << 1;
            QVariant displayData;
            QVariant editData;
            u8 flags = 0u;
        };

        QVector<Data> m_columnData;
};

QVector<QTreeWidgetItem *> get_checked_nodes(QTreeWidgetItem *node,
                                             Qt::CheckState checkState = Qt::Checked,
                                             int checkStateColumn = 0);

void get_checked_nodes(QVector<QTreeWidgetItem *> &dest,
                       QTreeWidgetItem *root,
                       Qt::CheckState checkState = Qt::Checked,
                       int column = 0);

using SetOfVoidStar = QSet<void *>;

void expand_tree_nodes(QTreeWidgetItem *root, const SetOfVoidStar &pointers,
                       int dataColumn = 0, const QVector<int> &dataRoles = { Qt::UserRole });

void expand_tree_nodes(QTreeWidgetItem *root, const SetOfVoidStar &pointers,
                       int dataColumn = 0, int dataRole = Qt::UserRole);

using NodeWalker = std::function<void (QTreeWidgetItem *node)>;

void walk_treewidget_nodes(QTreeWidgetItem *root, NodeWalker walker);

#endif /* __TREEWIDGET_UTIL_H__ */
