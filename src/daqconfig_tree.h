#ifndef __DAQCONFIG_TREE_H__
#define __DAQCONFIG_TREE_H__

#include <QWidget>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class TreeNode;

class DAQConfig;
class EventConfig;
class ModuleConfig;
class VMEScriptConfig;

class DAQConfigTreeWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQConfigTreeWidget(QWidget *parent = 0);

        void setConfig(DAQConfig *cfg);
        DAQConfig *getConfig() const;

    private:
        void addScriptNodes(TreeNode *parent, QList<VMEScriptConfig *> scripts, bool canDisable = false);
        void addEventNodes(TreeNode *parent, QList<EventConfig *> events);
        TreeNode *addEventNode(TreeNode *parent, EventConfig *event);
        TreeNode *addModuleNode(TreeNode *parent, ModuleConfig *module);

        void onItemClicked(QTreeWidgetItem *item, int column);
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onItemChanged(QTreeWidgetItem *item, int column);
        void onItemExpanded(QTreeWidgetItem *item);
        void treeContextMenu(const QPoint &pos);

        void onEventAdded(EventConfig *config);
        void onEventAboutToBeRemoved(EventConfig *config);

        void onModuleAdded(ModuleConfig *config);
        void onModuleAboutToBeRemoved(ModuleConfig *config);

        DAQConfig *m_config = nullptr;
        QTreeWidget *m_tree;
        QMap<QObject *, TreeNode *> m_treeMap;
        TreeNode *m_nodeEvents, *m_nodeManual, *m_nodeStart, *m_nodeEnd,
                 *m_nodeScripts;
};

#endif /* __DAQCONFIG_TREE_H__ */
