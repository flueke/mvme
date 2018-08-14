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
#ifndef __DAQCONFIG_TREE_H__
#define __DAQCONFIG_TREE_H__

#include <QWidget>
#include <QMap>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

class TreeNode;
class ConfigObject;
class VMEConfig;
class EventConfig;
class ModuleConfig;
class VMEScriptConfig;
class MVMEContext;

class EventNode;

class VMEConfigTreeWidget: public QWidget
{
    Q_OBJECT
    signals:
        //void configObjectClicked(ConfigObject *object);
        //void configObjectDoubleClicked(ConfigObject *object);
        void showDiagnostics(ModuleConfig *cfg);

    public:
        VMEConfigTreeWidget(MVMEContext *context, QWidget *parent = 0);

        void setConfig(VMEConfig *cfg);
        VMEConfig *getConfig() const;

    private:
        TreeNode *addScriptNode(TreeNode *parent, VMEScriptConfig *script);
        TreeNode *addEventNode(TreeNode *parent, EventConfig *event);
        TreeNode *addModuleNodes(EventNode *parent, ModuleConfig *module);

        void onItemClicked(QTreeWidgetItem *item, int column);
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onItemChanged(QTreeWidgetItem *item, int column);
        void onItemExpanded(QTreeWidgetItem *item);
        void treeContextMenu(const QPoint &pos);

        void onEventAdded(EventConfig *config);
        void onEventAboutToBeRemoved(EventConfig *config);

        void onModuleAdded(ModuleConfig *config);
        void onModuleAboutToBeRemoved(ModuleConfig *config);

        void onScriptAdded(VMEScriptConfig *script, const QString &category);
        void onScriptAboutToBeRemoved(VMEScriptConfig *script);

        // context menu action implementations
        void addEvent();
        void removeEvent();
        void editEvent();

        void addModule();
        void removeModule();
        void editModule();

        void addGlobalScript();
        void removeGlobalScript();
        void runScripts();
        void editName();
        void initModule();
        void onActionShowAdvancedChanged();
        void handleShowDiagnostics();
        void dumpVMUSBRegisters();
        void exploreWorkspace();
        void showEditNotes();
        void toggleObjectEnabled(QTreeWidgetItem *node, int expectedNodeType);
        bool isObjectEnabled(QTreeWidgetItem *node, int expectedNodeType) const;

        void runScriptConfigs(const QVector<VMEScriptConfig *> &configs);

        void updateConfigLabel();

        MVMEContext *m_context = nullptr;
        VMEConfig *m_config = nullptr;
        QTreeWidget *m_tree;
        // Maps config objects to tree nodes
        QMap<QObject *, TreeNode *> m_treeMap;

        TreeNode *m_nodeEvents, *m_nodeManual, *m_nodeStart, *m_nodeStop,
                 *m_nodeScripts;

        QAction *action_showAdvanced;

        QToolButton *pb_new, *pb_load, *pb_save, *pb_saveAs, *pb_notes;
        QLineEdit *le_fileName;
};

#endif /* __DAQCONFIG_TREE_H__ */
