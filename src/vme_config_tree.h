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
#include "globals.h"
#include "vme_config.h"
#include "vme_controller.h"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

class TreeNode;

class EventNode;

class VMEConfigTreeWidget: public QWidget
{
    Q_OBJECT
    signals:
        void showDiagnostics(ModuleConfig *cfg);
        void activateObjectWidget(QObject *object);
        void editVMEScript(VMEScriptConfig *vmeScript, const QString &metaTag = {});
        void addEvent();
        void editEvent(EventConfig *eventConfig);
        void runScriptConfigs(const QVector<VMEScriptConfig *> &scriptConfigs);
        void logMessage(const QString &msg);
        void dumpVMEControllerRegisters();

    public:
        VMEConfigTreeWidget(QWidget *parent = 0);
        // This makes use of the action defined in the MVMEMainWindow class.
        // Call this after the actions have been added to this widget via
        // QWidget::addAction().
        void setupActions();

        VMEConfig *getConfig() const;

    public slots:
        void setConfig(VMEConfig *cfg);
        void setConfigFilename(const QString &filename);
        void setWorkspaceDirectory(const QString &dirname);
        void setDAQState(const DAQState &daqState);
        void setVMEControllerState(const ControllerState &state);
        void setVMEController(const VMEController *ctrl);

    private slots:
        void editEventImpl();
        void onVMEControllerTypeSet(const VMEControllerType &t);

    private:
        TreeNode *addScriptNode(TreeNode *parent, VMEScriptConfig *script);
        TreeNode *addEventNode(TreeNode *parent, EventConfig *event);
        TreeNode *addModuleNodes(EventNode *parent, ModuleConfig *module);

        TreeNode *makeObjectNode(ConfigObject *obj);
        TreeNode *addObjectNode(QTreeWidgetItem *parentNode, ConfigObject *obj);
        void addContainerNodes(QTreeWidgetItem *parent, ContainerObject *obj);

        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onItemChanged(QTreeWidgetItem *item, int column);
        void onItemExpanded(QTreeWidgetItem *item);
        void treeContextMenu(const QPoint &pos);

        void onEventAdded(EventConfig *config, bool expandNode);
        void onEventAboutToBeRemoved(EventConfig *config);

        void onModuleAdded(ModuleConfig *config);
        void onModuleAboutToBeRemoved(ModuleConfig *config);

        void onScriptAdded(VMEScriptConfig *script, const QString &category);
        void onScriptAboutToBeRemoved(VMEScriptConfig *script);

        // context menu action implementations
        void removeEvent();

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
        void exploreWorkspace();
        void showEditNotes();
        void toggleObjectEnabled(QTreeWidgetItem *node, int expectedNodeType);
        bool isObjectEnabled(QTreeWidgetItem *node, int expectedNodeType) const;

        void updateConfigLabel();

        ConfigObject *getCurrentConfigObject() const;
        void copyToClipboard(const ConfigObject *obj);
        void pasteFromClipboard();
        bool canCopy(const ConfigObject *obj) const;
        bool canPaste() const;

        VMEConfig *m_config = nullptr;
        QString m_configFilename;
        QString m_workspaceDirectory;
        DAQState m_daqState = DAQState::Idle;
        ControllerState m_vmeControllerState = ControllerState::Disconnected;
        const VMEController *m_vmeController = nullptr;

        QTreeWidget *m_tree;
        // Maps config objects to tree nodes
        QMap<QObject *, TreeNode *> m_treeMap;

        TreeNode *m_nodeMVLCTriggerIO = nullptr,
                 *m_nodeDAQStart = nullptr,
                 *m_nodeEvents = nullptr,
                 *m_nodeDAQStop = nullptr,
                 *m_nodeManual = nullptr;

        QAction *action_showAdvanced,
                *action_dumpVMEControllerRegisters;

        QToolButton *pb_new, *pb_load, *pb_save, *pb_saveAs, *pb_notes;
        QLineEdit *le_fileName;
};

#endif /* __DAQCONFIG_TREE_H__ */
