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
    protected:
        void paint ( QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index ) const;
        QSize sizeHint ( const QStyleOptionViewItem & option, const QModelIndex & index ) const;
};

#endif /* __TREEWIDGET_UTIL_H__ */
