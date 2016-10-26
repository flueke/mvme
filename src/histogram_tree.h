#ifndef __HISTOGRAM_TREE_H__
#define __HISTOGRAM_TREE_H__

#include <QWidget>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

class MVMEContext;
class TreeNode;
class DAQConfig;
class AnalysisConfig;
class ModuleConfig;

class HistogramTreeWidget: public QWidget
{
    Q_OBJECT
    signals:
        void objectClicked(QObject *object);
        void objectDoubleClicked(QObject *object);
        void openInNewWindow(QObject *object);

    public:
        HistogramTreeWidget(MVMEContext *context, QWidget *parent = 0);

    private:
        void onObjectAdded(QObject *object);
        void onObjectAboutToBeRemoved(QObject *object);
        void onAnyConfigChanged();
        void onModuleAdded(ModuleConfig *config);
        void onModuleAboutToBeRemoved(ModuleConfig *config);

        void onItemClicked(QTreeWidgetItem *item, int column);
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onItemChanged(QTreeWidgetItem *item, int column);
        void onItemExpanded(QTreeWidgetItem *item);
        void treeContextMenu(const QPoint &pos);

        void clearHistogram();
        void removeHistogram();
        void add2DHistogram();
        void generateDefaultFilters();

        MVMEContext *m_context = nullptr;
        DAQConfig *m_daqConfig = nullptr;
        AnalysisConfig *m_analysisConfig = nullptr;
        QTreeWidget *m_tree;
        QMap<QObject *, TreeNode *> m_treeMap;
        TreeNode *m_node1D, *m_node2D;

        QPushButton *pb_generateDefaultFilters;
};

#endif /* __HISTOGRAM_TREE_H__ */
