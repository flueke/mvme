#ifndef __HISTOGRAM_TREE_H__
#define __HISTOGRAM_TREE_H__

#include <QWidget>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class MVMEContext;
class TreeNode;

class HistogramTreeWidget: public QWidget
{
    Q_OBJECT
    signals:
        void objectClicked(QObject *object);
        void objectDoubleClicked(QObject *object);

    public:
        HistogramTreeWidget(MVMEContext *context, QWidget *parent = 0);

    private:
        void onItemClicked(QTreeWidgetItem *item, int column);
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onItemChanged(QTreeWidgetItem *item, int column);
        void onItemExpanded(QTreeWidgetItem *item);
        void treeContextMenu(const QPoint &pos);

        MVMEContext *m_context = nullptr;
        QTreeWidget *m_tree;
        QMap<QObject *, TreeNode *> m_treeMap;
        TreeNode *m_node1D, *m_node2D;
};

#endif /* __HISTOGRAM_TREE_H__ */
