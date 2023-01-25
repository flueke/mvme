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
#include "vme_config_tree.h"
#include "mvlc_daq.h"
#include "vme_config_tree_p.h"

#include <QClipboard>
#include <QDebug>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <qnamespace.h>

#include "data_filter_edit.h"
#include "mvme.h"
#include "mvme_stream_worker.h"
#include "template_system.h"
#include "treewidget_utils.h"
#include "vme_config.h"
#include "vme_config_scripts.h"
#include "vme_config_ui.h"
#include "vme_config_util.h"
#include "vme_script_editor.h"
#include "vmusb.h"

using namespace std::placeholders;
using namespace vats;
using namespace mvme::vme_config;

static const QString VMEModuleConfigFileFilter = QSL("MVME Module Configs (*.mvmemodule *.json);; All Files (*.*)");
static const QString VMEEventConfigFileFilter = QSL("MVME Event Configs (*.mvmeevent *.json);; All Files (*.*)");

enum NodeType
{
    NodeType_Event = QTreeWidgetItem::UserType,     // 1000
    NodeType_Module,                                // 1001
    NodeType_ModuleReset,                           // 1002
    NodeType_EventModulesInit,
    NodeType_EventReadoutLoop,
    NodeType_EventStartStop,
    NodeType_VMEScript,                             // 1006
    NodeType_Container,
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_ScriptCategory,
};

class TreeNode: public QTreeWidgetItem
{
    public:
        explicit TreeNode(int type = QTreeWidgetItem::Type)
            : QTreeWidgetItem(type)
        {
            setFlags(flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
        }
};

class EventNode: public TreeNode
{
    public:
        EventNode()
            : TreeNode(NodeType_Event)
        {
            setIcon(0, QIcon(":/vme_event.png"));
        }

        TreeNode *modulesNode = nullptr;
        TreeNode *readoutLoopNode = nullptr;
        TreeNode *daqStartStopNode = nullptr;
};

class ModuleNode: public TreeNode
{
    public:
        ModuleNode()
            : TreeNode(NodeType_Module)
        {}

        TreeNode *readoutNode = nullptr;
};


bool is_parent_disabled(ConfigObject *obj)
{
    Q_ASSERT(obj);

    if (auto parentConfigObject = qobject_cast<ConfigObject *>(obj->parent()))
    {
        if (!parentConfigObject->isEnabled())
            return true;

        return is_parent_disabled(parentConfigObject);
    }

    return false;
}

bool should_draw_node_disabled(QTreeWidgetItem *node)
{
    if (auto obj = Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer)))
    {
        if (!obj->isEnabled())
            return true;

        return is_parent_disabled(obj);
    }

    return false;
}

class VMEConfigTreeItemDelegate: public QStyledItemDelegate
{
    public:
        explicit VMEConfigTreeItemDelegate(QObject* parent=0): QStyledItemDelegate(parent) {}

        virtual QWidget* createEditor(QWidget *parent,
                                      const QStyleOptionViewItem &option,
                                      const QModelIndex &index) const override
        {
            if (index.column() == 0)
            {
                return QStyledItemDelegate::createEditor(parent, option, index);
            }

            return nullptr;
        }

    protected:
        virtual void initStyleOption(QStyleOptionViewItem *option,
                                     const QModelIndex &index) const override
        {
            QStyledItemDelegate::initStyleOption(option, index);

            if (auto node = reinterpret_cast<QTreeWidgetItem *>(index.internalPointer()))
            {
                if (should_draw_node_disabled(node))
                {
                    option->state &= ~QStyle::State_Enabled;
                }
            }
        }
};

using NewAuxScriptCallback = std::function<void (VMEScriptConfig *newScript)>;

std::unique_ptr<QMenu> make_menu_new_aux_script(
    ContainerObject *destContainer,
    NewAuxScriptCallback callback = {},
    QWidget *parentWidget = nullptr)
{
    assert(destContainer);

    auto result = std::make_unique<QMenu>(parentWidget);
    auto auxInfos = vats::read_auxiliary_scripts();

    for (const auto &auxInfo: auxInfos)
    {
        auto action_triggered = [destContainer, callback, auxInfo] ()
        {
            auto scriptConfig = std::make_unique<VMEScriptConfig>();
            scriptConfig->setObjectName(auxInfo.scriptName());
            scriptConfig->setScriptContents(auxInfo.contents);
            auto raw = scriptConfig.release();
            destContainer->addChild(raw);
            if (callback)
                callback(raw);
        };

        result->addAction(auxInfo.scriptName(), destContainer, action_triggered);
    }

    return result;
}

std::unique_ptr<QMenu> make_menu_load_mvlc_trigger_io(
    VMEScriptConfig *destScriptConfig,
    QWidget *parentWidget = nullptr)
{
    static const auto ScriptPrefix = QSL("mvlc_trigger_io-");

    assert(destScriptConfig);

    auto result = std::make_unique<QMenu>(parentWidget);
    auto scripts = vats::read_mvlc_trigger_io_scripts();

    for (const auto &scriptInfo: scripts)
    {
        auto title = scriptInfo.fileInfo.baseName();

        if (title.startsWith(ScriptPrefix))
            title.remove(0, ScriptPrefix.size());

        title.replace('_', ' ');

        auto action_triggered = [destScriptConfig, scriptInfo] ()
        {
            destScriptConfig->setScriptContents(scriptInfo.contents);

            for (QObject *obj = destScriptConfig; obj; obj = obj->parent())
            {
                if (auto vmeConfig = qobject_cast<VMEConfig *>(obj))
                {
                    mesytec::mvme_mvlc::update_trigger_io_inplace(*vmeConfig);
                    break;
                }
            }
        };

        result->addAction(title, action_triggered);
    }

    return result;
}

void disable_drag_and_drop(QTreeWidgetItem *node)
{
    node->setFlags(node->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
}

void enable_drag_and_drop(QTreeWidgetItem *node)
{
    node->setFlags(node->flags() | (Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
}

QVector<QUuid> decode_id_list(QByteArray data) // FIXME: identical to the one in analysis/ui_eventwidget.cc
{
    QDataStream stream(&data, QIODevice::ReadOnly);
    QVector<QByteArray> sourceIds;
    stream >> sourceIds;

    QVector<QUuid> result;
    result.reserve(sourceIds.size());

    for (const auto &idData: sourceIds)
    {
        result.push_back(QUuid(idData));
    }

    return result;
}

// FIXME: identical to the one in analysis/ui_lib.
template<typename T>
T *get_pointer(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return node ? reinterpret_cast<T *>(node->data(0, dataRole).value<void *>()) : nullptr;
}

// FIXME: identical to the one in analysis/ui_lib.
inline QObject *get_qobject(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return get_pointer<QObject>(node, dataRole);
}

static const QString VMEConfigObjectIdListMIMEType = QSL("application/x-mvme-vmeconfig-object-id-list");

VMEConfigTree::VMEConfigTree(VMEConfigTreeWidget *configWidget)
    : m_configWidget(configWidget)
{
}

QStringList VMEConfigTree::mimeTypes() const
{
    return { VMEConfigObjectIdListMIMEType };
}

std::vector<ConfigObject *> get_vme_config_objects_from_id_list(const VMEConfig *vmeConfig, const QByteArray &data)
{
    std::vector<ConfigObject *> result;

    auto ids = decode_id_list(data);

    for (auto id: ids)
    {
        auto sourceObject = vmeConfig->findChildByPredicate<ConfigObject *>(
            [&id](ConfigObject *obj) { return obj->getId() == id; });
        if (sourceObject)
            result.push_back(sourceObject);
    }

    return result;
}

ConfigObject * get_first_vme_config_object_from_id_list(const VMEConfig *vmeConfig, const QByteArray &data)
{
    auto objects = get_vme_config_objects_from_id_list(vmeConfig, data);

    if (!objects.empty())
        return objects.at(0);
    return nullptr;
}

QMimeData *VMEConfigTree::mimeData(const QList<QTreeWidgetItem *> items) const
{
    QVector<QByteArray> idData;

    for (auto item: items)
    {
        switch (item->type())
        {
            case NodeType_VMEScript:
                if (auto source = get_pointer<VMEScriptConfig>(item, DataRole_Pointer))
                    idData.push_back(source->getId().toByteArray());
                break;

            case NodeType_Container:
                if (auto source = get_pointer<ContainerObject>(item, DataRole_Pointer))
                    idData.push_back(source->getId().toByteArray());
                break;

            case NodeType_Module:
                if (auto source = get_pointer<ModuleConfig>(item, DataRole_Pointer))
                    idData.push_back(source->getId().toByteArray());
                break;

            case NodeType_Event:
                if (auto source = get_pointer<EventConfig>(item, DataRole_Pointer))
                    idData.push_back(source->getId().toByteArray());
                break;

            default:
                break;
        }
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << idData;

    auto result = new QMimeData;
    result->setData(VMEConfigObjectIdListMIMEType, buffer);

    return result;
}

bool VMEConfigTree::dropMimeData(
    QTreeWidgetItem *parentItem,
    int parentIndex,
    const QMimeData *data,
    Qt::DropAction action)
{
    qDebug() << __PRETTY_FUNCTION__
        << "parentItem=" << parentItem
        << ", parentIndex=" << parentIndex
        << ", data=" << data
        << ", action=" << action;

    if (!(action == Qt::MoveAction /*|| action == Qt::CopyAction*/))
        return false;

    if (!parentItem)
        return false;

    // Drop onto a "Modules Init" node
    if (parentItem->type() == NodeType::NodeType_EventModulesInit)
    {
        return dropMimeDataOnModulesInit(parentItem, parentIndex, data, action);
    }

    if (parentItem->type() != NodeType_Container)
        return false;

    auto destContainer = get_pointer<ContainerObject>(parentItem);

    if (!destContainer)
        return false;

    const auto mimeType = VMEConfigObjectIdListMIMEType;

    if (!data->hasFormat(mimeType))
        return false;

    auto ids = decode_id_list(data->data(mimeType));

    if (ids.isEmpty())
        return false;

    auto vmeConfig = m_configWidget->getConfig();

    if (!vmeConfig)
        return false;

    // Collect source objects (Note: this can currently only be a single object
    // because multi selections are not possible).
    std::vector<ConfigObject *> sourceObjects;

    for (auto sid: ids)
    {
        auto &root = vmeConfig->getGlobalObjectRoot();

        auto sourceObject = root.findChildByPredicate<ConfigObject *>(
            [&sid](ConfigObject *obj) { return obj->getId() == sid; });

        if (sourceObject)
        {
            qDebug() << "found sourceObject" << sourceObject;
            sourceObjects.push_back(sourceObject);
        }
    }

    if (sourceObjects.empty() || sourceObjects.size() > 1)
        return false;

    assert(sourceObjects.size() == 1);

    auto sourceObject = sourceObjects[0];

    auto sourceParentContainer = qobject_cast<ContainerObject *>(sourceObject->parent());

    if (!sourceParentContainer)
        return false;

    qDebug() << "moving" << sourceObject << "from parentContainer" << sourceParentContainer
        << "into destContainer" << destContainer;

    sourceParentContainer->removeChild(sourceObject);
    destContainer->addChild(sourceObject, parentIndex);

    // FIXME: move/copy the config objects here

    // Always return false to stop the QTreeWidget implementation from updating
    // its internal model. We update things ourselves.
    return false;
}

bool VMEConfigTree::dropMimeDataOnModulesInit(
    QTreeWidgetItem *parentItem,
    int parentIndex,
    const QMimeData *data,
    Qt::DropAction /*action*/)
{
    assert(parentItem && parentItem->type() == NodeType_EventModulesInit);

    if (m_configWidget->getDAQState() != DAQState::Idle)
        return false;

    // parentItem is the destination "Modules Init" node, parentIndex is the
    // index the module should be placed at once dropped.
    // The parent node of parentItem is the destination event node.
    auto destEventNode = reinterpret_cast<EventNode *>(parentItem->parent());

    if (!destEventNode || destEventNode->type() != NodeType_Event)
        return false;

    auto destEvent = Var2Ptr<EventConfig>(destEventNode->data(0, DataRole_Pointer));

    if (!destEvent)
        return false;

    const auto mimeType = VMEConfigObjectIdListMIMEType;

    if (!data->hasFormat(mimeType))
        return false;

    auto vmeConfig = m_configWidget->getConfig();

    if (!vmeConfig)
        return false;

    auto droppedObj = get_first_vme_config_object_from_id_list(vmeConfig, data->data(mimeType));

    if (!droppedObj || !qobject_cast<ModuleConfig *>(droppedObj))
        return false;

    auto moduleConfig = qobject_cast<ModuleConfig *>(droppedObj);

    if (!moduleConfig)
        return false;

#if 0
    // XXX: Limit drag and drop for module to the same VME Event. The only
    // reason for this is that the analysis operators attached to a module
    // being moved between events will be run in the wrong event context after
    // the move.
    if (moduleConfig->getEventConfig() != destEvent)
        return false;
#endif
    // Move the event and emit a signal notifying observers about the change.
    auto sourceEvent = moduleConfig->getEventConfig();
    move_module(moduleConfig, destEvent, parentIndex);
    emit m_configWidget->moduleMoved(moduleConfig, sourceEvent, destEvent);

#ifndef QT_NO_DEBUG
    // Consistency check: module, module readout and the actual module order in
    // the event config have to be the same.

    if (auto parentEvent = moduleConfig->getEventConfig())
    {
        auto modules = parentEvent->getModuleConfigs();
        auto modulesNode = destEventNode->modulesNode;
        auto readoutLoopNode = destEventNode->readoutLoopNode;

        for (int moduleIndex=0; moduleIndex < modules.size(); ++moduleIndex)
        {
            int readoutNodeIndex = moduleIndex + 1;

            auto module = modules.at(moduleIndex);
            auto moduleNode = modulesNode->child(moduleIndex);
            auto readoutNode = readoutLoopNode->child(readoutNodeIndex);

            assert(module);
            assert(moduleNode);
            assert(readoutNode);

            assert(get_pointer<ModuleConfig>(moduleNode, DataRole_Pointer)
                   == module);

            assert(get_pointer<VMEScriptConfig>(readoutNode, DataRole_Pointer)
                   == module->getReadoutScript());
        }
    }

#endif


    // Always return false to stop the QTreeWidget implementation from updating
    // its internal model. We update things ourselves.
    return false;
}

VMEConfigTreeWidget::VMEConfigTreeWidget(QWidget *parent)
    : QWidget(parent)
    , m_tree(new VMEConfigTree(this))
{
    m_tree->setColumnCount(2);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(10);
    m_tree->setItemDelegate(new VMEConfigTreeItemDelegate(this));
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);

    // dragndrop
    m_tree->setDragEnabled(true);
    m_tree->viewport()->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);

    // copy keyboard shortcut
    {
        auto copyShortcut = new QShortcut(QKeySequence::Copy, m_tree);
        copyShortcut->setContext(Qt::WidgetShortcut);

        connect(copyShortcut, &QShortcut::activated,
                m_tree, [this] ()
                {
                    if (auto co = getCurrentConfigObject())
                        if (canCopy(co))
                            copyToClipboard(co);
                });
    }

    // paste keyboard shortcut
    {
        auto pasteShortcut = new QShortcut(QKeySequence::Paste, m_tree);
        pasteShortcut->setContext(Qt::WidgetShortcut);

        connect(pasteShortcut, &QShortcut::activated,
                m_tree, [this] ()
                {
                    if (canPaste())
                        pasteFromClipboard();
                });
    }

    auto headerItem = m_tree->headerItem();
    headerItem->setText(0, QSL("Object"));
    headerItem->setText(1, QSL("Info"));

    // Toolbar buttons
    pb_new    = make_action_toolbutton();
    pb_load   = make_action_toolbutton();
    pb_save   = make_action_toolbutton();
    pb_saveAs = make_action_toolbutton();
    pb_editVariables = make_action_toolbutton();

    QToolButton *pb_moreMenu = nullptr;

    {
        auto menu = new QMenu(this);
        this->menu_more = menu;
        action_showAdvanced = menu->addAction(QSL("Show advanced objects"));
        action_showAdvanced->setCheckable(true);
        connect(action_showAdvanced, &QAction::changed, this,
                &VMEConfigTreeWidget::onActionShowAdvancedChanged);

        action_dumpVMEControllerRegisters = menu->addAction(QSL("Dump VME Controller Registers"));
        connect(action_dumpVMEControllerRegisters, &QAction::triggered,
                this, &VMEConfigTreeWidget::dumpVMEControllerRegisters);
        action_dumpVMEControllerRegisters->setEnabled(false);

        auto action_exploreWorkspace = menu->addAction(QIcon(":/folder_orange.png"),
                                                       QSL("Explore Workspace"));
        connect(action_exploreWorkspace, &QAction::triggered,
                this, &VMEConfigTreeWidget::exploreWorkspace);

        pb_moreMenu = make_toolbutton(QSL(":/tree-settings.png"), QSL("More"));
        pb_moreMenu->setMenu(menu);
        pb_moreMenu->setPopupMode(QToolButton::InstantPopup);

        QSettings settings;
        action_showAdvanced->setChecked(settings.value("DAQTree/ShowAdvanced", true).toBool());
        onActionShowAdvancedChanged();
    }

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(pb_new);
    buttonLayout->addWidget(pb_load);
    buttonLayout->addWidget(pb_save);
    buttonLayout->addWidget(pb_saveAs);
    {
        auto sep = make_separator_frame(Qt::Vertical);
        sep->setFrameShadow(QFrame::Sunken);
        buttonLayout->addWidget(sep);
    }
    buttonLayout->addWidget(pb_editVariables);
    {
        auto sep = make_separator_frame(Qt::Vertical);
        sep->setFrameShadow(QFrame::Sunken);
        buttonLayout->addWidget(sep);
    }
    buttonLayout->addWidget(pb_moreMenu);
    buttonLayout->addStretch(1);

    // filename label
    le_fileName = new QLineEdit;
    le_fileName->setReadOnly(true);
    auto pal = le_fileName->palette();
    pal.setBrush(QPalette::Base, QColor(239, 235, 231));
    le_fileName->setPalette(pal);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttonLayout);
    layout->addWidget(le_fileName);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, &VMEConfigTreeWidget::onCurrentItemChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &VMEConfigTreeWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemActivated, this, &VMEConfigTreeWidget::onItemActivated);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &VMEConfigTreeWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged, this, &VMEConfigTreeWidget::onItemChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &VMEConfigTreeWidget::onItemExpanded);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &VMEConfigTreeWidget::treeContextMenu);

    action_editVariables = new QAction(
        QIcon(QSL(":/pencil.png")), QSL("Edit Variables"), this);
    action_editVariables->setEnabled(false);

    connect(action_editVariables, &QAction::triggered,
            this, [this] ()
            {
                auto node = m_tree->currentItem();

                if (node && node->type() == NodeType_Event)
                {
                    auto eventConfig = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
                    emit editEventVariables(eventConfig);
                }
            });

    pb_editVariables->setDefaultAction(action_editVariables);
}

void VMEConfigTreeWidget::setupGlobalActions()
{
    auto actions_ = actions();

    auto find_action = [&actions_] (const QString &name) -> QAction *
    {
        auto it = std::find_if(
            std::begin(actions_), std::end(actions_),
            [&name] (const QAction *action) {
                return action->objectName() == name;
            });

        return it != std::end(actions_) ? *it : nullptr;
    };

    pb_new->setDefaultAction(find_action("actionNewVMEConfig"));
    pb_load->setDefaultAction(find_action("actionOpenVMEConfig"));
    pb_save->setDefaultAction(find_action("actionSaveVMEConfig"));
    pb_saveAs->setDefaultAction(find_action("actionSaveVMEConfigAs"));
    if (auto action = find_action("actionExportVMEConfig"))
        menu_more->addAction(action);
}

void VMEConfigTreeWidget::setConfig(VMEConfig *cfg)
{
    // Cleanup
    if (m_config)
        m_config->disconnect(this);

    // Clear the tree and the lookup mapping
    qDeleteAll(m_tree->invisibleRootItem()->takeChildren());
    m_treeMap.clear();

    m_nodeMVLCTriggerIO = nullptr;
    m_nodeDAQStart = nullptr;
    m_nodeEvents = nullptr;
    m_nodeDAQStop = nullptr;
    m_nodeManual = nullptr;


    // Recreate objects
    m_config = cfg;

    auto startScriptContainer = cfg->getGlobalObjectRoot().findChild<ContainerObject *>(
        "daq_start");
    auto stopScriptContainer = cfg->getGlobalObjectRoot().findChild<ContainerObject *>(
        "daq_stop");
    auto manualScriptContainer = cfg->getGlobalObjectRoot().findChild<ContainerObject *>(
        "manual");

    m_nodeDAQStart = addObjectNode(m_tree->invisibleRootItem(), startScriptContainer);

    m_nodeEvents = new TreeNode;
    m_nodeEvents->setText(0,  QSL("Events"));
    m_nodeEvents->setIcon(0, QIcon(":/mvme_16x16.png"));
    m_tree->addTopLevelItem(m_nodeEvents);
    m_nodeEvents->setExpanded(true);

    if (cfg)
    {
        // Repopulate events
        for (auto event: cfg->getEventConfigs())
            onEventAdded(event, false);
    }

    m_nodeDAQStop = addObjectNode(m_tree->invisibleRootItem(), stopScriptContainer);
    m_nodeManual = addObjectNode(m_tree->invisibleRootItem(), manualScriptContainer);

    // These nodes are valid drop targets for containers and scripts.
    for (auto node: { m_nodeDAQStart, m_nodeDAQStop, m_nodeManual })
        node->setFlags(node->flags() | (Qt::ItemIsDropEnabled));

    if (cfg)
    {
        auto &cfg = m_config;

        connect(cfg, &VMEConfig::eventAdded,
                this, [this] (EventConfig *eventConfig) {
                    onEventAdded(eventConfig, true);
                });

        connect(cfg, &VMEConfig::eventAboutToBeRemoved,
                this, &VMEConfigTreeWidget::onEventAboutToBeRemoved);

        connect(cfg, &VMEConfig::modifiedChanged,
                this, &VMEConfigTreeWidget::updateConfigLabel);

        connect(cfg, &VMEConfig::vmeControllerTypeSet,
                this, &VMEConfigTreeWidget::onVMEControllerTypeSet);

        connect(cfg, &VMEConfig::globalChildAdded,
                this, &VMEConfigTreeWidget::onGlobalChildAdded);

        connect(cfg, &VMEConfig::globalChildAboutToBeRemoved,
                this, &VMEConfigTreeWidget::onGlobalChildAboutToBeRemoved);

        // Controller specific setup
        onVMEControllerTypeSet(cfg->getControllerType());
    }

    m_tree->resizeColumnToContents(0);
    updateConfigLabel();
}

void VMEConfigTreeWidget::onVMEControllerTypeSet(const VMEControllerType &t)
{
    if (!m_config) return;

    auto &cfg = m_config;

    delete m_nodeMVLCTriggerIO;
    m_nodeMVLCTriggerIO = nullptr;

    if (is_mvlc_controller(t))
    {
        auto mvlcTriggerIO = cfg->getMVLCTriggerIOScript();

        m_nodeMVLCTriggerIO = makeObjectNode(mvlcTriggerIO);
        m_nodeMVLCTriggerIO->setFlags(m_nodeMVLCTriggerIO->flags() & ~Qt::ItemIsEditable);
        m_treeMap[mvlcTriggerIO] = m_nodeMVLCTriggerIO;
        m_tree->insertTopLevelItem(0, m_nodeMVLCTriggerIO);
    }
}

void VMEConfigTreeWidget::onGlobalChildAdded(ConfigObject *globalChild, int parentIndex)
{
    qDebug() << __PRETTY_FUNCTION__ << "globalChild=" << globalChild
        << "nodeForChild=" << m_treeMap.value(globalChild);

    // Do nothing if a node for this child already exists.
    //
    // This can happen for example if a container hierarchy is pasted. This
    // method will be called with globalChild set to the root container. Below
    // addObjectNode() will be invoked for that root container which in turn
    // calls makeObjectNode() which recurses to the containers children.  Next
    // this slot will be invoked for the first child of the root for which a
    // node will just have been created.
    if (m_treeMap.value(globalChild))
    {
        qDebug() << __PRETTY_FUNCTION__ << "got a node for globalChild" << globalChild << "returning";
        return;
    }

    if (auto parentObject = qobject_cast<ConfigObject *>(globalChild->parent()))
    {
        if (auto parentNode = m_treeMap.value(parentObject))
        {
            addObjectNode(parentNode, parentIndex, globalChild);
            parentNode->setExpanded(true);
        }
    }
}

void VMEConfigTreeWidget::onGlobalChildAboutToBeRemoved(ConfigObject *globalChild)
{
    qDebug() << __PRETTY_FUNCTION__ << "child=" << globalChild;
    delete m_treeMap.take(globalChild);
}

VMEConfig *VMEConfigTreeWidget::getConfig() const
{
    return m_config;
}

template<typename T>
TreeNode *makeNode(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_Pointer, Ptr2Var(data));
    return ret;
}

TreeNode *VMEConfigTreeWidget::addScriptNode(TreeNode *parent, VMEScriptConfig* script)
{
    auto node = new TreeNode(NodeType_VMEScript);
    node->setData(0, DataRole_Pointer, Ptr2Var(script));
    node->setText(0, script->objectName());
    node->setIcon(0, QIcon(":/vme_script.png"));
    node->setFlags(node->flags() | Qt::ItemIsEditable);
    m_treeMap[script] = node;
    parent->addChild(node);

    return node;
}

TreeNode *VMEConfigTreeWidget::addEventNode(TreeNode *parent, EventConfig *event)
{
    auto eventNode = new EventNode;
    eventNode->setData(0, DataRole_Pointer, Ptr2Var(event));
    eventNode->setText(0, event->objectName());
    //eventNode->setCheckState(0, Qt::Checked);
    eventNode->setFlags(eventNode->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
    m_treeMap[event] = eventNode;
    parent->addChild(eventNode);
    eventNode->setExpanded(true);

    eventNode->modulesNode = new TreeNode(NodeType_EventModulesInit);
    auto modulesNode = eventNode->modulesNode;
    modulesNode->setText(0, QSL("Modules Init"));
    modulesNode->setIcon(0, QIcon(":/config_category.png"));
    modulesNode->setFlags(modulesNode->flags() | Qt::ItemIsDropEnabled);
    eventNode->addChild(modulesNode);
    modulesNode->setExpanded(true);

    eventNode->readoutLoopNode = new TreeNode(NodeType_EventReadoutLoop);
    auto readoutLoopNode = eventNode->readoutLoopNode;
    readoutLoopNode->setText(0, QSL("Readout Loop"));
    readoutLoopNode->setIcon(0, QIcon(":/config_category.png"));
    eventNode->addChild(readoutLoopNode);

    {
        auto node = makeNode(event->vmeScripts["readout_start"]);
        node->setText(0, QSL("Cycle Start"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        readoutLoopNode->addChild(node);
    }

    {
        auto node = makeNode(event->vmeScripts["readout_end"]);
        node->setText(0, QSL("Cycle End"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        readoutLoopNode->addChild(node);
    }

    eventNode->daqStartStopNode = new TreeNode(NodeType_EventStartStop);
    auto daqStartStopNode = eventNode->daqStartStopNode;
    daqStartStopNode->setText(0, QSL("Multicast DAQ Start/Stop"));
    daqStartStopNode->setIcon(0, QIcon(":/config_category.png"));
    eventNode->addChild(daqStartStopNode);

    {
        auto node = makeNode(event->vmeScripts["daq_start"]);
        node->setText(0, QSL("DAQ Start"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        daqStartStopNode->addChild(node);
    }

    {
        auto node = makeNode(event->vmeScripts["daq_stop"]);
        node->setText(0, QSL("DAQ Stop"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        daqStartStopNode->addChild(node);
    }

    return eventNode;
}

TreeNode *VMEConfigTreeWidget::addModuleNodes(
    EventNode *parent, ModuleConfig *module, int moduleIndex)
{
    auto moduleNode = new ModuleNode;
    moduleNode->setData(0, DataRole_Pointer, Ptr2Var(module));
    moduleNode->setText(0, module->objectName());
    //moduleNode->setCheckState(0, Qt::Checked);
    moduleNode->setIcon(0, QIcon(":/vme_module.png"));
    moduleNode->setFlags(moduleNode->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
    m_treeMap[module] = moduleNode;
    parent->modulesNode->insertChild(moduleIndex, moduleNode);

    // Module reset node
    {
        auto script = module->getResetScript();
        auto node = makeNode(script, NodeType_ModuleReset);
        node->setText(0, script->objectName());
        node->setIcon(0, QIcon(":/vme_script.png"));
        moduleNode->addChild(node);
    }

    // Module init nodes
    for (auto script: module->getInitScripts())
    {
        auto node = makeNode(script);
        node->setText(0, script->objectName());
        node->setIcon(0, QIcon(":/vme_script.png"));
        moduleNode->addChild(node);
    }

    // Readout node under the "Readout Loop" parent
    {
        auto readoutNode = makeNode(module->getReadoutScript());
        moduleNode->readoutNode = readoutNode;
        readoutNode->setText(0, module->objectName());
        readoutNode->setIcon(0, QIcon(":/vme_module.png"));

        auto readoutLoopNode = parent->readoutLoopNode;
        // Plus one to the index to account for the "Cycle Start" node
        readoutLoopNode->insertChild(moduleIndex+1, readoutNode);
    }

    return moduleNode;
}

TreeNode *VMEConfigTreeWidget::makeObjectNode(ConfigObject *obj)
{
    int nodeType = 0;

    if (qobject_cast<EventConfig *>(obj))
        nodeType = NodeType_Event;
    if (qobject_cast<ModuleConfig *>(obj))
        nodeType = NodeType_Module;
    if (qobject_cast<VMEScriptConfig *>(obj))
        nodeType = NodeType_VMEScript;
    if (qobject_cast<ContainerObject *>(obj))
        nodeType = NodeType_Container;

    auto treeNode = new TreeNode(nodeType);

    treeNode->setData(0, DataRole_Pointer, Ptr2Var(obj));
    treeNode->setText(0, obj->objectName());

    if (obj->property("display_name").isValid())
        treeNode->setText(0, obj->property("display_name").toString());

    if (obj->property("icon").isValid())
        treeNode->setIcon(0, QIcon(obj->property("icon").toString()));

    if (auto containerObject = qobject_cast<ContainerObject *>(obj))
    {
        auto cp = qobject_cast<ContainerObject *>(containerObject->parent());

        // Containers that are not directly below the 'global object root' of
        // the vme config are user-created directories. These can be renamed.
        if (cp != &m_config->getGlobalObjectRoot())
        {
            treeNode->setFlags(treeNode->flags() | Qt::ItemIsEditable);
        }

        // handle the containers children
        addContainerNodes(treeNode, containerObject);
    }

    if (qobject_cast<VMEScriptConfig *>(obj))
        treeNode->setFlags(treeNode->flags() | Qt::ItemIsEditable);

    return treeNode;
}

TreeNode *VMEConfigTreeWidget::addObjectNode(
    QTreeWidgetItem *parentNode,
    int parentIndex,
    ConfigObject *obj)
{
    auto treeNode = makeObjectNode(obj);

    if (!parentNode || parentNode != m_tree->invisibleRootItem())
    {
        qDebug() << __PRETTY_FUNCTION__ << "enabling dnd for" << treeNode << obj;
        enable_drag_and_drop(treeNode);
    }

    parentNode->insertChild(parentIndex, treeNode);
    m_treeMap[obj] = treeNode;

    return treeNode;
}

TreeNode *VMEConfigTreeWidget::addObjectNode(
    QTreeWidgetItem *parentNode,
    ConfigObject *obj)
{
    auto treeNode = makeObjectNode(obj);

    if (!parentNode || parentNode != m_tree->invisibleRootItem())
    {
        qDebug() << __PRETTY_FUNCTION__ << "enabling dnd for" << treeNode << obj;
        enable_drag_and_drop(treeNode);
    }

    parentNode->addChild(treeNode);
    m_treeMap[obj] = treeNode;

    return treeNode;
}

void VMEConfigTreeWidget::addContainerNodes(QTreeWidgetItem *parent, ContainerObject *obj)
{
    auto children = obj->getChildren();

    for (int i=0; i<children.size(); i++)
        addObjectNode(parent, i, children[i]);
}

void VMEConfigTreeWidget::onCurrentItemChanged(
    QTreeWidgetItem *node, QTreeWidgetItem *prev)
{
    action_editVariables->setEnabled(node && node->type() == NodeType_Event);
    qDebug() << __PRETTY_FUNCTION__ << node << prev;
}

void VMEConfigTreeWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << __PRETTY_FUNCTION__ << item << column;

    auto nodes = m_treeMap.values();

    auto it = std::find(std::begin(nodes), std::end(nodes), item);

    if (it != nodes.end())
    {
        qDebug() << __PRETTY_FUNCTION__
            << "item=" << item
            << "column=" << column
            << "nodeForItem=" << *it;
    }
    else
    {
        qDebug() << __PRETTY_FUNCTION__
            << "item=" << item
            << "column=" << column
            << "nodeForItem=None!";
    }
}

void VMEConfigTreeWidget::onItemActivated(QTreeWidgetItem *item, int column)
{
    qDebug() << __PRETTY_FUNCTION__ << item << column;
}

void VMEConfigTreeWidget::onItemDoubleClicked(QTreeWidgetItem *item, int /* column */)
{
    auto configObject = Var2Ptr<ConfigObject>(item->data(0, DataRole_Pointer));
    auto scriptConfig = qobject_cast<VMEScriptConfig *>(configObject);

    if (scriptConfig)
    {
        try
        {
            auto metaTag = vme_script::get_first_meta_block_tag(
                mesytec::mvme::parse(scriptConfig));

            emit editVMEScript(scriptConfig, metaTag);
            return;
        }
        catch (const vme_script::ParseError &e) { }

        emit editVMEScript(scriptConfig);
    }
}

/* Called when the contents in the column of the item change.
 * Used to implement item renaming. */
void VMEConfigTreeWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    auto obj = Var2Ptr<ConfigObject>(item->data(0, DataRole_Pointer));

    if (obj && column == 0)
    {
        if (item->flags() & Qt::ItemIsEditable)
        {
            obj->setObjectName(item->text(0));
        }

        m_tree->resizeColumnToContents(0);
    }
}

void VMEConfigTreeWidget::onItemExpanded(QTreeWidgetItem * /*item*/)
{
    m_tree->resizeColumnToContents(0);
}

bool is_child_of(const QObject *obj, const QStringList &rootNames)
{
    if (auto parent = obj->parent())
    {
        if (rootNames.contains(parent->objectName()))
            return true;

        return is_child_of(parent, rootNames);
    }

    return false;
}

// Recurse down towards the leaves setting the enabled state of each object
// including the root.
void recursively_set_objects_enabled(ConfigObject *root, bool enabled)
{
    root->setEnabled(enabled);

    if (auto container = qobject_cast<ContainerObject *>(root))
    {
        for (auto child: container->getChildren())
            recursively_set_objects_enabled(child, enabled);
    }
}

void VMEConfigTreeWidget::treeContextMenu(const QPoint &pos)
{
    if (!m_config) return;

    auto node = m_tree->itemAt(pos);
    auto obj = node ? Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer)) : nullptr;
    auto vmeScript = qobject_cast<VMEScriptConfig *>(obj);
    bool isIdle = (m_daqState == DAQState::Idle);
    bool isMVLC = is_mvlc_controller(m_config->getControllerType());
    QAction *action = nullptr;

    QMenu menu;

    //
    // Script nodes
    //
    if (vmeScript)
    {
        if (isIdle || isMVLC)
        {
            menu.addAction(QIcon(":/script-run.png"), QSL("Run Script"),
                           this, &VMEConfigTreeWidget::runScripts);
        }

        menu.addAction(QIcon(QSL(":/pencil.png")), QSL("Edit Script"),
                       this, &VMEConfigTreeWidget::editScript);

        try
        {
            // For scripts that have a meta tag and thus probably open up in a unique-widget (e.g.
            // trigger io gui) add an extra entry to allow editing in the standard script editor
            // instead.
            auto metaTag = vme_script::get_first_meta_block_tag(
                mesytec::mvme::parse(vmeScript));

            if (!metaTag.isEmpty())
                menu.addAction(QIcon(QSL(":/pencil.png")), QSL("Edit as VMEScript text"),
                               this, &VMEConfigTreeWidget::editScriptInEditor);
        }
        catch (const vme_script::ParseError &e) { }
    }

    //
    // Events
    //
    if (node == m_nodeEvents)
    {
        if (isIdle)
        {
            menu.addAction(QIcon(":/vme_event.png"), QSL("Add Event"), this, &VMEConfigTreeWidget::addEvent);
            menu.addAction(QIcon(":/vme_event.png"), QSL("Add Event from file"), this, &VMEConfigTreeWidget::loadEventFromFile);
        }
    }

    if (node && node->type() == NodeType_Event)
    {
        auto eventConfig = dynamic_cast<EventConfig *>(obj);
        assert(eventConfig);

        action = menu.addAction(QIcon(QSL(":/gear.png")), QSL("Edit Event Settings"),
                                this, &VMEConfigTreeWidget::editEventImpl);
        action->setEnabled(isIdle);

        menu.addAction(
            QIcon(QSL(":/pencil.png")), QSL("Edit Event Variables"),
            this, [this] ()
            {
                auto node = m_tree->currentItem();

                if (node && node->type() == NodeType_Event)
                {
                    auto eventConfig = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
                    emit editEventVariables(eventConfig);
                }
            });

        menu.addAction(QIcon(QSL(":/document-rename.png")), QSL("Rename Event"),
                       this, &VMEConfigTreeWidget::editName);

        menu.addAction(QIcon(":/vme_event.png"), QSL("Merge with Event from file"),
                       this, &VMEConfigTreeWidget::loadEventFromFile);

        menu.addAction(QIcon(QSL(":/document-save.png")), "Save Event to file",
                       [this, eventConfig]
                       { saveEventToFile(eventConfig); });

        menu.addSeparator();

        action = menu.addAction(QIcon(QSL(":/vme_module.png")), QSL("Add Module"),
                                this, &VMEConfigTreeWidget::addModule);
        action->setEnabled(isIdle);

        action = menu.addAction(QIcon(QSL(":/vme_module.png")), QSL("Add Module from file"),
                                this, &VMEConfigTreeWidget::addModuleFromFile);
        action->setEnabled(isIdle);

    }

    // Add Module
    if (node && node->type() == NodeType_EventModulesInit)
    {
        if (isIdle)
        {
            menu.addAction(QIcon(QSL(":/vme_module.png")), QSL("Add Module"),
                           this, &VMEConfigTreeWidget::addModule);
            menu.addAction(QIcon(QSL(":/vme_module.png")), QSL("Add Module from file"),
                           this, &VMEConfigTreeWidget::addModuleFromFile);
        }
    }

    // Module Node
    if (node && node->type() == NodeType_Module)
    {
        Q_ASSERT(obj);

        if ((isIdle || isMVLC) && obj->isEnabled())
        {
            menu.addAction(QIcon(QSL(":/gear.png")), QSL("Edit Module Settings"),
                           this, &VMEConfigTreeWidget::editModule);
            menu.addSeparator();
            menu.addAction(QSL("Init Module"), this, &VMEConfigTreeWidget::initModule);
            menu.addAction(QSL("Reset Module"), this, &VMEConfigTreeWidget::resetModule);
        }

        if (isIdle)
        {
            menu.addSeparator();
            menu.addAction(
                obj->isEnabled() ? QSL("Disable Module") : QSL("Enable Module"),
                this, [this, node]() { toggleObjectEnabled(node, NodeType_Module); });
        }

        menu.addAction(QIcon(QSL(":/document-rename.png")), QSL("Rename Module"),
                       this, &VMEConfigTreeWidget::editName);

        if (obj->isEnabled())
        {
            menu.addAction(QSL("Show Diagnostics"), this,
                           &VMEConfigTreeWidget::handleShowDiagnostics);
        }
    }

    // MVLC Trigger IO (note: this is a VMEScript node but with a custom icon)
    if (node == m_nodeMVLCTriggerIO)
    {
        auto triggerIOScript = Var2Ptr<VMEScriptConfig>(node->data(0, DataRole_Pointer));
        auto scriptsMenu = make_menu_load_mvlc_trigger_io(triggerIOScript, &menu);

        if (!scriptsMenu->isEmpty())
        {
            auto action = menu.addAction(
                QIcon(":/vme_script.png"), QSL("Load script from library"));
            action->setMenu(scriptsMenu.release());
        }
    }

    //
    // Global scripts (DAQ Start, DAQ Stop, Manual)
    //

    if (auto parentContainer = qobject_cast<ContainerObject *>(obj))
    {
        if (isIdle || isMVLC)
        {
            if (node->childCount() > 0)
            {
                menu.addAction(QIcon(":/script-run.png"), QSL("Run Scripts"),
                               this, &VMEConfigTreeWidget::runScripts);
                menu.addSeparator();
            }
        }

        menu.addAction(QIcon(":/vme_script.png"), QSL("Add Script"),
                       this, &VMEConfigTreeWidget::addGlobalScript);

        {
            // Callback invoked after an auxiliary script has been added via
            // the menu. Selects the newly created node.
            auto callback = [this] (VMEScriptConfig *newScript)
            {
                if (auto node = m_treeMap.value(newScript))
                {
                    m_tree->resizeColumnToContents(0);
                    m_tree->clearSelection();
                    m_tree->setCurrentItem(node);
                    node->setSelected(true);
                }
            };

            auto menuAuxScripts = make_menu_new_aux_script(parentContainer, callback, &menu);

            if (!menuAuxScripts->isEmpty())
            {
                auto actionAddAuxScript = menu.addAction(
                    QIcon(":/vme_script.png"), QSL("Add Script from library"));
                actionAddAuxScript->setMenu(menuAuxScripts.release());
            }
        }

        menu.addAction(QIcon(":/folder_orange.png"), QSL("Add Directory"),
                       this, &VMEConfigTreeWidget::addScriptDirectory);
    }

    if (auto script = qobject_cast<const VMEScriptConfig *>(obj))
    {
        menu.addAction(QIcon(":/document-save-as.png"), QSL("Save script"),
            this, [this, script] { mvme::vme_config::gui_save_vme_script_config_to_file(script, this); });
    }

    // - Rename for scripts if they are under any of the global nodes.
    // - Rename for directories if they are under any of the global nodes.
    // - Recursive enable/disable of trees if they are under any of the global
    //   nodes.  Note: disabling scripts under "manual" doesn't really make
    //   sense but it makes the code quite a bit simpler especially as the case
    //   where disabled objects hierarchies are pasted under "manual" does not
    //   need special handling.

    QStringList rootNames = { "daq_start", "daq_stop", "manual" };

    if (obj && is_child_of(obj, rootNames))
    {
        QString actionName = "Rename ";

        if (qobject_cast<VMEScriptConfig *>(obj))
            actionName += "Script";
        else if (qobject_cast<ContainerObject *>(obj))
            actionName += "Directory";

        menu.addAction(QIcon(QSL(":/document-rename.png")), actionName,
                       this, &VMEConfigTreeWidget::editName);
        menu.addSeparator();

        bool enable = !obj->isEnabled();

        menu.addAction(
            enable ? QSL("Enable Scripts") : QSL("Disable Scripts"),
            this, [obj, enable] { recursively_set_objects_enabled(obj, enable); });
    }

    auto make_object_type_string = [](const ConfigObject *obj)
    {
        if (qobject_cast<const EventConfig *>(obj))
            return QSL("Event");
        if (qobject_cast<const ModuleConfig *>(obj))
            return QSL("Module");
        if (qobject_cast<const VMEScriptConfig *>(obj))
            return QSL("VME Script");
        if (qobject_cast<const ContainerObject *>(obj))
            return QSL("Directory");
        return QString{};
    };

    // copy and paste
    {
        menu.addSeparator();

        auto action = menu.addAction(
            QIcon::fromTheme("edit-copy"), "Copy " + make_object_type_string(obj),
            [this, obj] { copyToClipboard(obj); },
            QKeySequence::Copy);

        action->setEnabled(canCopy(obj));

        QString pasteObjectTypeString;

        if (canPaste())
        {
            auto clipboardData = QGuiApplication::clipboard()->mimeData();
            auto objFromClipboard = make_object_from_mime_data_or_json_text(clipboardData);

            pasteObjectTypeString = make_object_type_string(objFromClipboard.get());
        }

        action = menu.addAction(
            QIcon::fromTheme("edit-paste"), "Paste " + pasteObjectTypeString,
            [this] { pasteFromClipboard(); },
            QKeySequence::Paste);

        action->setEnabled(canPaste());
    }

    if (node && node->type() == NodeType_Module)
    {
        auto moduleConfig = dynamic_cast<ModuleConfig *>(obj);
        assert(moduleConfig);

        if (moduleConfig)
        {
            menu.addSeparator();

            menu.addAction(
                QIcon(QSL(":/document-save.png")),
                "Save Module to file",
                [this, moduleConfig] { saveModuleToFile(moduleConfig); });
        }
    }

    // remove selected object
    if (isIdle)
    {
        QString objectTypeString;
        std::function<void ()> removeFunc;

        if (node && node->type() == NodeType_Event)
        {
            objectTypeString = "Event";
            removeFunc = [this] () { removeEvent(); };
        }

        if (node && node->type() == NodeType_Module)
        {
            objectTypeString = "Module";
            removeFunc = [this] () { removeModule(); };
        }

        if (qobject_cast<VMEScriptConfig *>(obj) && obj->parent() && node != m_nodeMVLCTriggerIO)
        {
            if (auto parentContainer = qobject_cast<ContainerObject *>(obj->parent()))
            {
                (void) parentContainer;
                objectTypeString = "VME Script";
                removeFunc = [this] () { removeGlobalScript(); };
            }
        }

        if (qobject_cast<ContainerObject *>(obj))
        {
            // Cannot remove the daq_start, daq_stop and manual nodes
            if (obj->parent() != &m_config->getGlobalObjectRoot())
            {
                objectTypeString = "Directory";
                removeFunc = [this] () { removeDirectoryRecursively(); };
            }
        }

        if (removeFunc)
        {
            menu.addSeparator();

            menu.addAction(
                QIcon::fromTheme("edit-delete"),
                "Remove " + objectTypeString,
                removeFunc);
        }
    }

    if (!menu.isEmpty())
        menu.exec(m_tree->mapToGlobal(pos));
}

void VMEConfigTreeWidget::onEventAdded(EventConfig *eventConfig, bool expandNode)
{
    addEventNode(m_nodeEvents, eventConfig);

    int moduleIndex = 0;
    for (auto module: eventConfig->getModuleConfigs())
        onModuleAdded(module, moduleIndex++);

    connect(eventConfig, &EventConfig::moduleAdded,
            this, &VMEConfigTreeWidget::onModuleAdded);

    connect(eventConfig, &EventConfig::moduleAboutToBeRemoved,
            this, &VMEConfigTreeWidget::onModuleAboutToBeRemoved);

    auto updateEventNode = [eventConfig, this](bool isModified)
    {
        auto node = static_cast<EventNode *>(m_treeMap.value(eventConfig, nullptr));

        if (!isModified || !node)
            return;

        node->setText(0, eventConfig->objectName());
        //node->setCheckState(0, eventConfig->isEnabled() ? Qt::Checked : Qt::Unchecked);

        QString infoText;

        switch (eventConfig->triggerCondition)
        {
            case TriggerCondition::Interrupt:
                {
                    infoText = QString("Trigger=IRQ%1")
                        .arg(eventConfig->irqLevel);
                } break;
            case TriggerCondition::NIM1:
                {
                    infoText = QSL("Trigger=NIM");
                } break;
            case TriggerCondition::Periodic:
                {
                    infoText = QSL("Trigger=Periodic");
                    if (is_mvlc_controller(m_config->getControllerType()))
                    {
                        auto tp = eventConfig->getMVLCTimerPeriod();
                        infoText += QSL(", every %1%2").arg(tp.first).arg(tp.second);
                    }
                } break;
            default:
                {
                    infoText = QString("Trigger=%1")
                        .arg(TriggerConditionNames.value(eventConfig->triggerCondition));
                } break;
        }

        node->setText(1, infoText);
    };

    updateEventNode(true);

    if (expandNode)
    {
        if (auto node = m_treeMap.value(eventConfig, nullptr))
            node->setExpanded(true);
    }

    connect(eventConfig, &EventConfig::modified, this, updateEventNode);
    onActionShowAdvancedChanged();
}

void VMEConfigTreeWidget::onEventAboutToBeRemoved(EventConfig *config)
{
    for (auto module: config->getModuleConfigs())
    {
        onModuleAboutToBeRemoved(module);
    }

    delete m_treeMap.take(config);
}

void VMEConfigTreeWidget::onModuleAdded(ModuleConfig *module, int moduleIndex)
{
    auto eventNode = static_cast<EventNode *>(m_treeMap[module->parent()]);
    addModuleNodes(eventNode, module, moduleIndex);

    auto updateModuleNodes = [module, this](bool isModified) {
        auto node = static_cast<ModuleNode *>(m_treeMap.value(module, nullptr));

        if (!isModified || !node)
            return;

        node->setText(0, module->objectName());
        node->readoutNode->setText(0, module->objectName());
        //node->setCheckState(0, module->isEnabled() ? Qt::Checked : Qt::Unchecked);

        QString infoText = QString("Type=%1, Address=0x%2")
            .arg(module->getModuleMeta().displayName)
            .arg(module->getBaseAddress(), 8, 16, QChar('0'));

        node->setText(1, infoText);
    };

    updateModuleNodes(true);

    connect(module, &ModuleConfig::modified, this, updateModuleNodes);
    onActionShowAdvancedChanged();


}

void VMEConfigTreeWidget::onModuleAboutToBeRemoved(ModuleConfig *module)
{
    auto moduleNode = static_cast<ModuleNode *>(m_treeMap[module]);
    delete moduleNode->readoutNode;
    delete m_treeMap.take(module);
}

//
// Context menu action implementations
//

void VMEConfigTreeWidget::removeEvent()
{
    auto node = m_tree->currentItem();

    if (node && node->type() == NodeType_Event)
    {
        auto event = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
        m_config->removeEventConfig(event);
        mesytec::mvme_mvlc::update_trigger_io_inplace(*m_config);
        event->deleteLater();
    }
}

void VMEConfigTreeWidget::toggleObjectEnabled(QTreeWidgetItem *node, int expectedNodeType)
{
    if (node)
    {
        qDebug() << __PRETTY_FUNCTION__ << node << node->type() << expectedNodeType
            << node->text(0);
        Q_ASSERT(node->type() == expectedNodeType);
    }

    if (node && node->type() == expectedNodeType)
    {
        if (auto obj = Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer)))
        {
            obj->setEnabled(!obj->isEnabled());
        }
    }
}

bool VMEConfigTreeWidget::isObjectEnabled(QTreeWidgetItem *node, int expectedNodeType) const
{
    if (node && node->type() == expectedNodeType)
    {
        if (auto obj = Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer)))
        {
            return obj->isEnabled();
        }
    }

    return false;
}

void VMEConfigTreeWidget::editEventImpl()
{
    auto node = m_tree->currentItem();

    if (node && node->type() == NodeType_Event)
    {
        auto eventConfig = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
        emit editEvent(eventConfig);
    }
}

// TODO: refactor in the same way as done to addEvent(): make it a signal and
// move the implementation elsewhere
void VMEConfigTreeWidget::addModule()
{
    auto node = m_tree->currentItem();

    while (node && node->type() != NodeType_Event)
    {
        node = node->parent();
    }

    if (node)
    {
        auto event = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
        bool doExpand = (event->getModuleConfigs().size() == 0);

        auto mod = std::make_unique<ModuleConfig>();
        ModuleConfigDialog dialog(mod.get(), event, m_config, this);
        dialog.setWindowTitle(QSL("Add Module"));
        int result = dialog.exec();

        if (result == QDialog::Accepted)
        {
            event->addModuleConfig(mod.release());

            if (doExpand)
                static_cast<EventNode *>(node)->modulesNode->setExpanded(true);
        }
    }
}

void VMEConfigTreeWidget::removeModule()
{
    auto node = m_tree->currentItem();

    while (node && node->type() != NodeType_Module)
    {
        node = node->parent();
    }

    if (node)
    {
        auto module = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
        auto event = qobject_cast<EventConfig *>(module->parent());
        if (event)
        {
            event->removeModuleConfig(module);
            module->deleteLater();
        }
    }
}

void VMEConfigTreeWidget::editModule()
{
    auto node = m_tree->currentItem();

    while (node && node->type() != NodeType_Module)
    {
        node = node->parent();
    }

    if (node)
    {
        auto moduleConfig = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
        ModuleConfigDialog dialog(moduleConfig, moduleConfig->getEventConfig(), m_config, this);
        dialog.setWindowTitle(QSL("Edit Module"));
        dialog.exec();
    }
}

void VMEConfigTreeWidget::saveModuleToFile(const ModuleConfig *mod)
{
    // Show a dialog to input custom values for vendorName, typeName, etc.
    // Then use these values for the meta data.
    auto meta = mod->getModuleMeta();

    // Get the module variables and build a JSON structure as used in the
    // vmmr_monitor/module_info.json file.
    auto vars = mod->getVariables();

    // Note(230125): the special variable handling here is only done so that the
    // variables and their values can be stored in the ModuleMeta data in the
    // output json file. Check if this is actually needed as this information is
    // also written out with the module config itself via the default
    // ConfigObject::write() mechanism.
    QJsonArray varsArray;

    for (const auto &varName: vars.symbols.keys())
    {
        QJsonObject varJ;
        varJ["name"] = varName;
        varJ["value"] = vars.symbols[varName].value;
        varJ["comment"] = vars.symbols[varName].comment;
        varsArray.append(varJ);
    }

    {
        auto le_typeName = new QLineEdit;
        auto spin_typeId = new QSpinBox;
        spin_typeId->setMinimum(1);
        spin_typeId->setMaximum(255);
        auto le_displayName = new QLineEdit;
        auto le_vendorName = new QLineEdit;
        auto le_headerFilter = new DataFilterEdit();
        // Use the modules current VME address as a suggestion for the new default address.
        auto le_vmeAddress = make_vme_address_edit(mod->getBaseAddress());
        auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

        le_typeName->setText(meta.typeName);
        spin_typeId->setValue(meta.typeId);
        le_displayName->setText(meta.displayName);
        le_vendorName->setText(meta.vendorName);
        le_headerFilter->setFilterString(QString::fromLocal8Bit(meta.eventHeaderFilter));

        QDialog d;
        d.setWindowTitle("Save VME module to file");
        auto l = new QFormLayout(&d);
        l->addRow("Type Name", le_typeName);
        l->addRow("Unique Type ID", spin_typeId);
        l->addRow("Display Name", le_displayName);
        l->addRow("Vendor Name", le_vendorName);
        l->addRow("Default VME Address", le_vmeAddress);
        l->addRow("MultiEvent Header Filter", le_headerFilter);
        l->addRow(bb);

        QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);

        if (d.exec() != QDialog::Accepted)
            return;

        meta.typeName = le_typeName->text();
        meta.typeId = spin_typeId->value();
        meta.displayName = le_displayName->text();
        meta.vendorName = le_vendorName->text();
        meta.eventHeaderFilter = le_headerFilter->getFilterString().toLocal8Bit();
        meta.vmeAddress = le_vmeAddress->text().toUInt(nullptr, 16);
    }

    // Run a file save dialog
    auto path = QSettings().value("LastObjectSaveDirectory").toString();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    path += "/" + meta.typeName + ".mvmemodule";

    QFileDialog fd(this, "Save Module As", path, VMEModuleConfigFileFilter);

    fd.setDefaultSuffix(".mvmemodule");
    fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
        return;

    auto filename = fd.selectedFiles().front();

    // Serialize the module config to json
    QJsonObject modJ;
    mod->write(modJ);

    // Store parts of the VMEModuleMeta in a json object
    QJsonObject metaJ;
    metaJ["typeName"] = meta.typeName;
    metaJ["typeId"] = static_cast<int>(meta.typeId);
    metaJ["displayName"] = meta.displayName;
    metaJ["vendorName"] = meta.vendorName;
    metaJ["eventHeaderFilter"] = QString::fromLocal8Bit(meta.eventHeaderFilter);
    metaJ["vmeAddress"] = static_cast<qint64>(meta.vmeAddress);
    metaJ["variables"] = varsArray;

    // Outer container for both the module and the meta
    QJsonObject containerJ;
    containerJ["ModuleConfig"] = modJ;
    containerJ["ModuleMeta"] = metaJ;

    QJsonDocument doc(containerJ);

    QFile out(filename);

    if (!out.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QSL("Error opening %1 for writing").arg(filename));
        return;
    }

    if (out.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QSL("Error writing to %1: %2")
                              .arg(filename).arg(out.errorString()));
        return;
    }

    QSettings().setValue("LastObjectSaveDirectory", QFileInfo(filename).absolutePath());
}

// TODO: merge with VMEConfigTreeWidget::addModule()
void VMEConfigTreeWidget::addModuleFromFile()
{
    // Get the parent EventConfig
    auto node = m_tree->currentItem();

    while (node && node->type() != NodeType_Event)
        node = node->parent();

    if (!node)
        return;

    auto event = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));

    if (!event)
        return;

    bool doExpand = (event->getModuleConfigs().size() == 0);

    // Load the module from JSON file
    auto path = QSettings().value("LastObjectSaveDirectory").toString();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    auto filename = QFileDialog::getOpenFileName(
        this, "Load Module from file", path, VMEModuleConfigFileFilter);

    if (filename.isEmpty())
        return;

    auto doc = gui_read_json_file(filename);

    if (doc.isNull())
        return;

    auto mod = moduleconfig_from_modulejson(doc.object());

    // Now run the module config dialog on the newly loaded module
    ModuleConfigDialog dialog(mod.get(), event, m_config, this);
    dialog.setWindowTitle(QSL("Add Module"));
    int result = dialog.exec();

    if (result != QDialog::Accepted)
        return;

    event->addModuleConfig(mod.release());

    if (doExpand)
        static_cast<EventNode *>(node)->modulesNode->setExpanded(true);
}

void VMEConfigTreeWidget::saveEventToFile(const EventConfig *ev)
{
    // Run a file save dialog
    auto path = QSettings().value("LastObjectSaveDirectory").toString();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    path += "/" + ev->objectName() + ".mvmeevent";

    QFileDialog fd(this, "Save Event Config As", path, VMEEventConfigFileFilter);

    fd.setDefaultSuffix(".mvmeevent");
    fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
        return;

    auto filename = fd.selectedFiles().front();

    // Serialize the module config to json
    QJsonObject eventJ;
    ev->write(eventJ);

    // Outer container for both the module and the meta
    QJsonObject containerJ;
    containerJ["EventConfig"] = eventJ;

    QJsonDocument doc(containerJ);

    QFile out(filename);

    if (!out.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QSL("Error opening %1 for writing").arg(filename));
        return;
    }

    if (out.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QSL("Error writing to %1: %2")
                              .arg(filename).arg(out.errorString()));
        return;
    }

    QSettings().setValue("LastObjectSaveDirectory", QFileInfo(filename).absolutePath());
}

void VMEConfigTreeWidget::loadEventFromFile()
{
    // Two cases:
    // * if the global 'Events' node is the target then just add the loaded event to the vme config
    // * if an existing event config is the target then:
    //   - merge in the event variables overwriting existing variables (do not overwrite system variables, e.g. sys_irq!)
    //   - add each of the events child modules to the existing event

    auto node = m_tree->currentItem();

    if (!node)
        return;

    EventConfig *targetEvent = nullptr;

    if (node->type() == NodeType_Event)
    {
        if (!(targetEvent = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer))))
            return;
    }

    // Load the event from JSON file
    auto path = QSettings().value("LastObjectSaveDirectory").toString();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    auto filename = QFileDialog::getOpenFileName(
        this, "Load Event from file", path, VMEEventConfigFileFilter);

    if (filename.isEmpty())
        return;

    auto doc = gui_read_json_file(filename);

    if (doc.isNull())
        return;

    auto eventConfig = eventconfig_from_eventjson(doc.object());

    if (!targetEvent)
    {
        EventConfigDialog dialog(m_config->getControllerType(), eventConfig.get(), m_config, this);
        dialog.setWindowTitle("Add Event");
        if (dialog.exec() != QDialog::Accepted)
            return;
        m_config->addEventConfig(eventConfig.release());
        return;
    }

    auto sourceVariables = eventConfig->getVariables();

    for (const auto &varName: sourceVariables.symbolNameSet())
        targetEvent->setVariable(varName, sourceVariables[varName]);

    auto childModules = eventConfig->getModuleConfigs();

    for (auto moduleConfig: childModules)
    {
        eventConfig->removeModuleConfig(moduleConfig);
        targetEvent->addModuleConfig(moduleConfig);
    }
}

void VMEConfigTreeWidget::addGlobalScript()
{
    auto node = m_tree->currentItem();
    if (!node) return;

    auto obj  = Var2Ptr<ContainerObject>(node->data(0, DataRole_Pointer));
    if (!obj) return;

    auto script = new VMEScriptConfig;

    script->setObjectName("new vme script");
    script->setObjectName(make_unique_name(script, obj));
    obj->addChild(script);

    node->setExpanded(true);

    auto scriptNode = m_treeMap.value(script, nullptr);
    assert(scriptNode);

    if (scriptNode)
        m_tree->editItem(scriptNode, 0);
}

void VMEConfigTreeWidget::addScriptDirectory()
{
    auto node = m_tree->currentItem();
    if (!node) return;

    auto obj  = Var2Ptr<ContainerObject>(node->data(0, DataRole_Pointer));
    if (!obj) return;

    auto dir = make_directory_container(QSL("new directory")).release();
    dir->setObjectName(make_unique_name(dir, obj));
    qDebug() << __PRETTY_FUNCTION__ << ">> adding new" << dir << "to parent" << obj;
    obj->addChild(dir);
    qDebug() << __PRETTY_FUNCTION__ << "<< done add new dir";

    node->setExpanded(true);

    auto dirNode = m_treeMap.value(dir, nullptr);
    assert(dirNode);
    if (dirNode)
    {
        m_tree->clearSelection();
        dirNode->setSelected(true);
        m_tree->editItem(dirNode, 0);
    }
}

void VMEConfigTreeWidget::removeDirectoryRecursively()
{
    auto node = m_tree->currentItem();
    if (!node) return;

    auto obj  = Var2Ptr<ContainerObject>(node->data(0, DataRole_Pointer));
    if (!obj) return;

    assert(m_treeMap.value(obj) == node);

    auto pc = qobject_cast<ContainerObject *>(obj->parent());
    assert(pc);
    if (!pc) return;

    // Cannot remove the daq_start, daq_stop and manual nodes which are the
    // direct children of the global root.
    if (pc == &m_config->getGlobalObjectRoot())
        return;

    if (pc->removeChild(obj))
    {
        delete m_treeMap.take(obj);     // delete the node
        obj->deleteLater();             // delete the object
    }
}

void VMEConfigTreeWidget::removeGlobalScript()
{
    auto node = m_tree->currentItem();
    auto script = Var2Ptr<VMEScriptConfig>(node->data(0, DataRole_Pointer));
    if (script && m_config->removeGlobalScript(script))
    {
        delete m_treeMap.take(script);  // delete the node
        script->deleteLater();          // delete the script
    }
}

enum class CollectOption { CollectAll, CollectEnabled };

// Collect vme scripts in depth first order.
void collect_script_configs(QVector<VMEScriptConfig *> &dest, ConfigObject *root, const CollectOption &opt)
{
    if (auto script = qobject_cast<VMEScriptConfig *>(root))
    {
        if ((opt == CollectOption::CollectAll)
            || (opt == CollectOption::CollectEnabled && script->isEnabled()))
            dest.push_back(script);
    }
    else if (auto container = qobject_cast<ContainerObject *>(root))
    {
        for (auto child: container->getChildren())
            collect_script_configs(dest, child, opt);
    }
}

void VMEConfigTreeWidget::runScripts()
{
    auto node = m_tree->currentItem();
    auto obj  = Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer));

    QVector<VMEScriptConfig *> scriptConfigs;

    collect_script_configs(scriptConfigs, obj, CollectOption::CollectEnabled);

    emit runScriptConfigs(scriptConfigs);
}

void VMEConfigTreeWidget::editScript()
{
    auto node = m_tree->currentItem();

    auto obj  = Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer));

    if (auto scriptConfig = qobject_cast<VMEScriptConfig *>(obj))
    {
        try
        {
            auto metaTag = vme_script::get_first_meta_block_tag(
                mesytec::mvme::parse(scriptConfig));

            emit editVMEScript(scriptConfig, metaTag);
            return;
        }
        catch (const vme_script::ParseError &e) { }

        emit editVMEScript(scriptConfig);
    }
}

void VMEConfigTreeWidget::editScriptInEditor()
{
    auto node = m_tree->currentItem();

    auto obj  = Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer));

    if (auto scriptConfig = qobject_cast<VMEScriptConfig *>(obj))
        emit editVMEScript(scriptConfig);
}

void VMEConfigTreeWidget::editName()
{
    m_tree->editItem(m_tree->currentItem(), 0);
}

void VMEConfigTreeWidget::initModule()
{
    auto node = m_tree->currentItem();
    auto module = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    emit runScriptConfigs(module->getInitScripts());
}

void VMEConfigTreeWidget::resetModule()
{
    auto node = m_tree->currentItem();
    auto module = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    emit runScriptConfigs({ module->getResetScript() });
}

void VMEConfigTreeWidget::onActionShowAdvancedChanged()
{
    if (!m_nodeEvents) return;

    auto nodes = findItems(m_nodeEvents, [](QTreeWidgetItem *node) {
        return node->type() == NodeType_EventReadoutLoop
            || node->type() == NodeType_EventStartStop
            || node->type() == NodeType_ModuleReset;
    });

    bool showAdvanced = action_showAdvanced->isChecked();

    for (auto node: nodes)
        node->setHidden(!showAdvanced);

    QSettings settings;
    settings.setValue("DAQTree/ShowAdvanced", showAdvanced);
};

void VMEConfigTreeWidget::handleShowDiagnostics()
{
    auto node = m_tree->currentItem();
    auto module = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    emit showDiagnostics(module);
}

void VMEConfigTreeWidget::exploreWorkspace()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_workspaceDirectory));
}

void VMEConfigTreeWidget::setConfigFilename(const QString &filename)
{
    m_configFilename = filename;
    updateConfigLabel();
}

void VMEConfigTreeWidget::setWorkspaceDirectory(const QString &dirname)
{
    m_workspaceDirectory = dirname;
    updateConfigLabel();
}

void VMEConfigTreeWidget::setDAQState(const DAQState &daqState)
{
    m_daqState = daqState;
}

void VMEConfigTreeWidget::setVMEControllerState(const ControllerState &state)
{
    m_vmeControllerState = state;
    action_dumpVMEControllerRegisters->setEnabled(state == ControllerState::Connected);
}

void VMEConfigTreeWidget::setVMEController(const VMEController *ctrl)
{
    m_vmeController = ctrl;
}

void VMEConfigTreeWidget::updateConfigLabel()
{
    QString fileName = m_configFilename;

    if (fileName.isEmpty())
        fileName = QSL("<not saved>");

    if (m_config && m_config->isModified())
        fileName += QSL(" *");

    auto wsDir = m_workspaceDirectory + '/';

    if (fileName.startsWith(wsDir))
    {
        fileName.remove(wsDir);
    }

    le_fileName->setText(fileName);
    le_fileName->setToolTip(fileName);
    le_fileName->setStatusTip(fileName);
}

bool VMEConfigTreeWidget::canCopy(const ConfigObject *obj) const
{
    if (qobject_cast<const ContainerObject *>(obj)
        && obj->parent() == &m_config->getGlobalObjectRoot())
    {
        return false;
    }

    return can_mime_copy_object(obj);
}

bool VMEConfigTreeWidget::canPaste() const
{
    auto clipboardData = QGuiApplication::clipboard()->mimeData();
    auto node = m_tree->currentItem();
    auto nt = node ? node->type() : 0u;
    bool result = false;

    // First check the MIME types generated by mvme for which we allow pasting.

    if (clipboardData->hasFormat(MIMEType_JSON_VMEEventConfig))
    {
        result |= node == m_nodeEvents;
    }

    if (clipboardData->hasFormat(MIMEType_JSON_VMEModuleConfig))
    {
        result |= (nt == NodeType_Event
                   || nt == NodeType_EventModulesInit);
    }

    if (clipboardData->hasFormat(MIMEType_JSON_VMEScriptConfig))
    {
        result |= (node->type() == NodeType_Container);
    }

    if (clipboardData->hasFormat(MIMEType_JSON_ContainerObject))
    {
        result |= (node->type() == NodeType_Container);
    }

    // Early return in case we already got a positive answer. Skips the more
    // expensive steps below.
    if (result)
        return result;

    // Next check json text stored in the application/json or text/plain MIME
    // types. This requires actually creating an object from the json data so
    // it's more expensive than the tests above.

    std::unique_ptr<ConfigObject> obj = make_object_from_mime_data_or_json_text(
        clipboardData);

    result |= ((node == m_nodeEvents)
               && qobject_cast<EventConfig *>(obj.get()));

    result |= ((nt == NodeType_Event
                || nt == NodeType_EventModulesInit)
               && qobject_cast<ModuleConfig *>(obj.get()));

    result |= ((nt == NodeType_Container)
               && qobject_cast<VMEScriptConfig *>(obj.get()));

    return result;
}

ConfigObject *VMEConfigTreeWidget::getCurrentConfigObject() const
{
    if (auto node = m_tree->currentItem())
    {
        auto qobj = Var2Ptr<QObject>(node->data(0, DataRole_Pointer));
        return qobject_cast<ConfigObject *>(qobj);
    }

    return nullptr;
}

void VMEConfigTreeWidget::copyToClipboard(const ConfigObject *obj)
{
    assert(canCopy(obj));

    if (!obj || !can_mime_copy_object(obj))
        return;

    if (auto mimeData = make_mime_data(obj))
        QGuiApplication::clipboard()->setMimeData(mimeData.release());
}

void VMEConfigTreeWidget::pasteFromClipboard()
{
    if (!canPaste())
        return;

    auto clipboardData = QGuiApplication::clipboard()->mimeData();
    auto node = m_tree->currentItem();
    auto nt = node ? node->type() : 0u;
    auto destObj = node ? Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer)) : nullptr;

    auto obj = make_object_from_mime_data_or_json_text(clipboardData);

    if (!obj)
        return;

    qDebug() << __PRETTY_FUNCTION__ << "got an object from the clipboard:" << obj.get();

    generate_new_object_ids(obj.get());

    auto vmeConfig = getConfig();

    if (auto eventConfig = qobject_cast<EventConfig *>(obj.get()))
    {
        if (node != m_nodeEvents)
            return;

        eventConfig->setObjectName(make_unique_event_name("event", vmeConfig));

        // Tree is notified via VMEConfig::eventAdded()
        vmeConfig->addEventConfig(eventConfig);
        obj.release();
    }
    else if (auto moduleConfig = qobject_cast<ModuleConfig *>(obj.get()))
    {
        // Direct paste onto an event node
        EventConfig *destEvent = qobject_cast<EventConfig *>(destObj);

        // Check for paste onto the "Modules Init" node of an event
        if (!destEvent
            && nt == NodeType_EventModulesInit
            && node->parent()
            && node->parent()->type() == NodeType_Event)
        {
            destEvent = Var2Ptr<EventConfig>(node->parent()->data(0, DataRole_Pointer));
        }

        if (destEvent)
        {
            moduleConfig->setObjectName(
                make_unique_module_name(moduleConfig->getModuleMeta().typeName, vmeConfig));

            // Tree is notified via EventConfig::moduleAdded()
            destEvent->addModuleConfig(moduleConfig);
            obj.release();
        }
    }
    else
    {
        if (auto destContainer = qobject_cast<ContainerObject *>(destObj))
        {
            obj->setObjectName(make_unique_name(obj.get(), destContainer));

            destContainer->addChild(obj.get());
            obj.release();

            if (node)
                node->setExpanded(true);
        }
    }

    qDebug() << __PRETTY_FUNCTION__ << vmeConfig->getGlobalScriptCategories();
}
