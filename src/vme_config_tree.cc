/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "mvme.h"
#include "vme_config.h"
#include "mvme_context.h"
#include "config_ui.h"
#include "treewidget_utils.h"
#include "mvme_event_processor.h"
#include "vmusb.h"
#include "vme_script_editor.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>

using namespace std::placeholders;
using namespace vats;

enum NodeType
{
    NodeType_Event = QTreeWidgetItem::UserType,
    NodeType_Module,
    NodeType_ModuleReset,
    NodeType_EventModulesInit,
    NodeType_EventReadoutLoop,
    NodeType_EventStartStop,
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_ScriptCategory,
};

class TreeNode: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;
};

class EventNode: public TreeNode
{
    public:
        EventNode()
            : TreeNode(NodeType_Event)
        {}

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

VMEConfigTreeWidget::VMEConfigTreeWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_tree(new QTreeWidget(this))
    , m_nodeEvents(new TreeNode)
    , m_nodeManual(new TreeNode)
    , m_nodeStart(new TreeNode)
    , m_nodeStop(new TreeNode)
    , m_nodeScripts(new TreeNode)
{
    m_tree->setColumnCount(2);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(10);
    m_tree->setItemDelegateForColumn(1, new NoEditDelegate(this));
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);

    auto headerItem = m_tree->headerItem();
    headerItem->setText(0, QSL("Object"));
    headerItem->setText(1, QSL("Info"));

    m_nodeEvents->setText(0,        QSL("Events"));
    m_nodeScripts->setText(0,       QSL("Global Scripts"));

    m_nodeStart->setText(0, QSL("DAQ Start"));
    m_nodeStart->setData(0, DataRole_ScriptCategory, "daq_start");
    m_nodeStart->setIcon(0, QIcon(":/config_category.png"));

    m_nodeStop->setText(0, QSL("DAQ Stop"));
    m_nodeStop->setData(0, DataRole_ScriptCategory, "daq_stop");
    m_nodeStop->setIcon(0, QIcon(":/config_category.png"));

    m_nodeManual->setText(0,  QSL("Manual"));
    m_nodeManual->setData(0,  DataRole_ScriptCategory, "manual");
    m_nodeManual->setIcon(0, QIcon(":/config_category.png"));

    m_tree->addTopLevelItem(m_nodeEvents);
    m_tree->addTopLevelItem(m_nodeScripts);

    m_nodeScripts->addChild(m_nodeStart);
    m_nodeScripts->addChild(m_nodeStop);
    m_nodeScripts->addChild(m_nodeManual);

    auto nodes = QList<TreeNode *>({ m_nodeEvents, m_nodeScripts });
    for (auto node: nodes)
        node->setExpanded(true);

    m_tree->resizeColumnToContents(0);

    // Toolbar buttons
    auto makeToolButton = [](const QString &icon, const QString &text)
    {
        auto result = new QToolButton;
        result->setIcon(QIcon(icon));
        result->setText(text);
        result->setStatusTip(text);
        result->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        auto font = result->font();
        font.setPointSize(7);
        result->setFont(font);
        return result;
    };

    auto makeActionToolButton = [](QAction *action)
    {
        Q_ASSERT(action);
        auto result = new QToolButton;
        result->setDefaultAction(action);
        result->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        auto font = result->font();
        font.setPointSize(7);
        result->setFont(font);
        return result;
    };

    auto mainwin = m_context->getMainWindow();
    pb_new    = makeActionToolButton(mainwin->findChild<QAction *>("actionNewVMEConfig"));
    pb_load   = makeActionToolButton(mainwin->findChild<QAction *>("actionOpenVMEConfig"));
    pb_save   = makeActionToolButton(mainwin->findChild<QAction *>("actionSaveVMEConfig"));
    pb_saveAs = makeActionToolButton(mainwin->findChild<QAction *>("actionSaveVMEConfigAs"));
    //pb_notes  = makeToolButton(QSL(":/text-document.png"), QSL("Notes"));
    //connect(pb_notes, &QPushButton::clicked, this, &VMEConfigTreeWidget::showEditNotes);

    QToolButton *pb_treeSettings = nullptr;

    {
        auto menu = new QMenu(this);
        action_showAdvanced = menu->addAction(QSL("Show advanced objects"));
        action_showAdvanced->setCheckable(true);
        connect(action_showAdvanced, &QAction::changed, this, &VMEConfigTreeWidget::onActionShowAdvancedChanged);

        auto action_dumpVMUSBRegisters = menu->addAction(QSL("Dump VMUSB Registers"));
        connect(action_dumpVMUSBRegisters, &QAction::triggered, this, &VMEConfigTreeWidget::dumpVMUSBRegisters);

        pb_treeSettings = makeToolButton(QSL(":/tree-settings.png"), QSL("More"));
        pb_treeSettings->setMenu(menu);
        pb_treeSettings->setPopupMode(QToolButton::InstantPopup);

        QSettings settings;
        action_showAdvanced->setChecked(settings.value("DAQTree/ShowAdvanced", false).toBool());
        onActionShowAdvancedChanged();
    }

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(pb_new);
    buttonLayout->addWidget(pb_load);
    buttonLayout->addWidget(pb_save);
    buttonLayout->addWidget(pb_saveAs);
    buttonLayout->addWidget(pb_treeSettings);
    //buttonLayout->addWidget(pb_notes); TODO: implement this
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

    connect(m_tree, &QTreeWidget::itemClicked, this, &VMEConfigTreeWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &VMEConfigTreeWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged, this, &VMEConfigTreeWidget::onItemChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &VMEConfigTreeWidget::onItemExpanded);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &VMEConfigTreeWidget::treeContextMenu);

    connect(m_context, &MVMEContext::daqConfigChanged, this, &VMEConfigTreeWidget::setConfig);
    connect(m_context, &MVMEContext::daqConfigFileNameChanged, this, [this](const QString &) {
        updateConfigLabel();
    });

    setConfig(m_context->getVMEConfig());
}

void VMEConfigTreeWidget::setConfig(VMEConfig *cfg)
{
    qDeleteAll(m_nodeManual->takeChildren());
    qDeleteAll(m_nodeStart->takeChildren());
    qDeleteAll(m_nodeStop->takeChildren());
    qDeleteAll(m_nodeEvents->takeChildren());
    m_treeMap.clear();

    m_config = cfg;

    if (cfg)
    {
        for (auto category: cfg->vmeScriptLists.keys())
            for (auto script: cfg->vmeScriptLists[category])
                onScriptAdded(script, category);

        for (auto event: cfg->getEventConfigs())
            onEventAdded(event);

        connect(cfg, &VMEConfig::eventAdded, this, &VMEConfigTreeWidget::onEventAdded);
        connect(cfg, &VMEConfig::eventAboutToBeRemoved, this, &VMEConfigTreeWidget::onEventAboutToBeRemoved);
        connect(cfg, &VMEConfig::globalScriptAdded, this, &VMEConfigTreeWidget::onScriptAdded);
        connect(cfg, &VMEConfig::globalScriptAboutToBeRemoved, this, &VMEConfigTreeWidget::onScriptAboutToBeRemoved);
        connect(cfg, &VMEConfig::modifiedChanged, this, &VMEConfigTreeWidget::updateConfigLabel);
    }

    m_tree->resizeColumnToContents(0);
    updateConfigLabel();
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

TreeNode *VMEConfigTreeWidget::addScriptNode(TreeNode *parent, VMEScriptConfig* script, bool canDisable)
{
    auto node = new TreeNode;
    node->setData(0, DataRole_Pointer, Ptr2Var(script));
    node->setText(0, script->objectName());
    node->setIcon(0, QIcon(":/vme_script.png"));
    node->setFlags(node->flags() | Qt::ItemIsEditable);
    if (canDisable)
    {
        //node->setCheckState(0, script->isEnabled() ? Qt::Checked : Qt::Unchecked);
    }
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
    eventNode->setFlags(eventNode->flags() | Qt::ItemIsEditable);
    m_treeMap[event] = eventNode;
    parent->addChild(eventNode);
    eventNode->setExpanded(true);

    eventNode->modulesNode = new TreeNode(NodeType_EventModulesInit);
    auto modulesNode = eventNode->modulesNode;
    modulesNode->setText(0, QSL("Modules Init"));
    modulesNode->setIcon(0, QIcon(":/config_category.png"));
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

TreeNode *VMEConfigTreeWidget::addModuleNodes(EventNode *parent, ModuleConfig *module)
{
    auto moduleNode = new ModuleNode;
    moduleNode->setData(0, DataRole_Pointer, Ptr2Var(module));
    moduleNode->setText(0, module->objectName());
    //moduleNode->setCheckState(0, Qt::Checked);
    moduleNode->setIcon(0, QIcon(":/vme_module.png"));
    moduleNode->setFlags(moduleNode->flags() | Qt::ItemIsEditable);
    m_treeMap[module] = moduleNode;
    parent->modulesNode->addChild(moduleNode);

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

    {
        auto readoutNode = makeNode(module->getReadoutScript());
        moduleNode->readoutNode = readoutNode;
        readoutNode->setText(0, module->objectName());
        readoutNode->setIcon(0, QIcon(":/vme_module.png"));

        auto readoutLoopNode = parent->readoutLoopNode;
        readoutLoopNode->insertChild(readoutLoopNode->childCount() - 1, readoutNode);
    }

    return moduleNode;
}

void VMEConfigTreeWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    auto configObject = Var2Ptr<ConfigObject>(item->data(0, DataRole_Pointer));

    qDebug() << "clicked" << item << configObject;

    if (configObject)
    {
        m_context->activateObjectWidget(configObject);
    }
}

void VMEConfigTreeWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    auto configObject = Var2Ptr<ConfigObject>(item->data(0, DataRole_Pointer));
    auto scriptConfig = qobject_cast<VMEScriptConfig *>(configObject);

    if (scriptConfig)
    {
        if (m_context->hasObjectWidget(scriptConfig))
        {
            m_context->activateObjectWidget(scriptConfig);
        }
        else
        {
            auto widget = new VMEScriptEditor(m_context, scriptConfig);
            widget->setWindowIcon(QIcon(QPixmap(":/vme_script.png")));
            m_context->addObjectWidget(widget, scriptConfig, scriptConfig->getId().toString());
        }
    }
}

void VMEConfigTreeWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    auto obj = Var2Ptr<ConfigObject>(item->data(0, DataRole_Pointer));

    if (obj)
    {
        //if (item->flags() & Qt::ItemIsUserCheckable)
        //    obj->setEnabled(item->checkState(0) != Qt::Unchecked);

        if (item->flags() & Qt::ItemIsEditable)
        {
            obj->setObjectName(item->text(0));
        }

        m_tree->resizeColumnToContents(0);
    }
}

void VMEConfigTreeWidget::onItemExpanded(QTreeWidgetItem *item)
{
    m_tree->resizeColumnToContents(0);
}

void VMEConfigTreeWidget::treeContextMenu(const QPoint &pos)
{
    auto node = m_tree->itemAt(pos);
    auto parent = node ? node->parent() : nullptr;
    auto obj = node ? Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer)) : nullptr;
    bool isIdle = (m_context->getDAQState() == DAQState::Idle);

    QMenu menu;

    //
    // Script nodes
    //
    if (qobject_cast<VMEScriptConfig *>(obj))
    {
        if (isIdle)
            menu.addAction(QSL("Run Script"), this, &VMEConfigTreeWidget::runScripts);
    }

    //
    // Events
    //
    if (node == m_nodeEvents)
    {
        if (isIdle)
            menu.addAction(QSL("Add Event"), this, &VMEConfigTreeWidget::addEvent);
    }

    if (node && node->type() == NodeType_Event)
    {
        if (isIdle)
            menu.addAction(QSL("Edit Event"), this, &VMEConfigTreeWidget::editEvent);

        if (isIdle)
            menu.addAction(QSL("Add Module"), this, &VMEConfigTreeWidget::addModule);

        menu.addAction(QSL("Rename Event"), this, &VMEConfigTreeWidget::editName);

        if (isIdle)
        {
            menu.addSeparator();
            menu.addAction(QSL("Remove Event"), this, &VMEConfigTreeWidget::removeEvent);
        }
    }

    if (node && node->type() == NodeType_EventModulesInit)
    {
        if (isIdle)
            menu.addAction(QSL("Add Module"), this, &VMEConfigTreeWidget::addModule);
    }

    if (node && node->type() == NodeType_Module)
    {
        if (isIdle)
        {
            menu.addAction(QSL("Init Module"), this, &VMEConfigTreeWidget::initModule);
            menu.addAction(QSL("Edit Module"), this, &VMEConfigTreeWidget::editModule);
        }

        menu.addAction(QSL("Rename Module"), this, &VMEConfigTreeWidget::editName);

        if (isIdle)
        {
            menu.addSeparator();
            menu.addAction(QSL("Remove Module"), this, &VMEConfigTreeWidget::removeModule);
        }

        if (!m_context->getEventProcessor()->hasDiagnostics())
            menu.addAction(QSL("Show Diagnostics"), this, &VMEConfigTreeWidget::handleShowDiagnostics);
    }

    //
    // Global scripts
    //
    if (node == m_nodeStart || node == m_nodeStop || node == m_nodeManual)
    {
        if (isIdle)
        {
            if (node->childCount() > 0)
                menu.addAction(QSL("Run scripts"), this, &VMEConfigTreeWidget::runScripts);
        }

        menu.addAction(QSL("Add script"), this, &VMEConfigTreeWidget::addGlobalScript);

    }

    if (parent == m_nodeStart || parent == m_nodeStop || parent == m_nodeManual)
    {
        menu.addAction(QSL("Rename Script"), this, &VMEConfigTreeWidget::editName);
        if (isIdle)
        {
            menu.addSeparator();
            menu.addAction(QSL("Remove Script"), this, &VMEConfigTreeWidget::removeGlobalScript);
        }
    }

    if (!menu.isEmpty())
    {
        menu.exec(m_tree->mapToGlobal(pos));
    }
}

void VMEConfigTreeWidget::onEventAdded(EventConfig *eventConfig)
{
    addEventNode(m_nodeEvents, eventConfig);

    for (auto module: eventConfig->modules)
        onModuleAdded(module);

    connect(eventConfig, &EventConfig::moduleAdded, this, &VMEConfigTreeWidget::onModuleAdded);
    connect(eventConfig, &EventConfig::moduleAboutToBeRemoved, this, &VMEConfigTreeWidget::onModuleAboutToBeRemoved);

    auto updateEventNode = [eventConfig, this](bool isModified) {
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
                    infoText = QString("Trigger=IRQ, lvl=%2, vec=%3")
                        .arg(eventConfig->irqLevel)
                        .arg(eventConfig->irqVector);
                } break;
            case TriggerCondition::NIM1:
                {
                    infoText = QSL("Trigger=NIM");
                } break;
            case TriggerCondition::Periodic:
                {
                    infoText = QSL("Trigger=Periodic");
                } break;
            default:
                {
                    infoText = QString("Trigger=%1").arg(TriggerConditionNames.value(eventConfig->triggerCondition));
                } break;
        }

        node->setText(1, infoText);
    };

    updateEventNode(true);

    connect(eventConfig, &EventConfig::modified, this, updateEventNode);
    onActionShowAdvancedChanged();
}

void VMEConfigTreeWidget::onEventAboutToBeRemoved(EventConfig *config)
{
    for (auto module: config->modules)
    {
        onModuleAboutToBeRemoved(module);
    }

    delete m_treeMap.take(config);
}

void VMEConfigTreeWidget::onModuleAdded(ModuleConfig *module)
{
    auto eventNode = static_cast<EventNode *>(m_treeMap[module->parent()]);
    addModuleNodes(eventNode, module);

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

void VMEConfigTreeWidget::onScriptAdded(VMEScriptConfig *script, const QString &category)
{
    TreeNode *parentNode = nullptr;
    bool canDisable = true;

    if (category == QSL("daq_start"))
        parentNode = m_nodeStart;
    else if (category == QSL("daq_stop"))
        parentNode = m_nodeStop;
    else if (category == QSL("manual"))
    {
        parentNode = m_nodeManual;
        canDisable = false;
    }

    if (parentNode)
    {
        addScriptNode(parentNode, script, canDisable);
        m_tree->resizeColumnToContents(0);
    }
}

void VMEConfigTreeWidget::onScriptAboutToBeRemoved(VMEScriptConfig *script)
{
    delete m_treeMap.take(script);
}

//
// context menu action implementations
//
void VMEConfigTreeWidget::addEvent()
{
    auto config = new EventConfig;
    config->setObjectName(QString("event%1").arg(m_config->getEventConfigs().size()));
    EventConfigDialog dialog(m_context, m_context->getVMEController(), config, this);
    dialog.setWindowTitle(QSL("Add Event"));
    int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        if (config->triggerCondition != TriggerCondition::Periodic)
        {
            auto logger = [this](const QString &msg) { m_context->logMessage(msg); };
            VMEEventTemplates templates = read_templates(logger).eventTemplates;

            config->vmeScripts["daq_start"]->setScriptContents(templates.daqStart.contents);
            config->vmeScripts["daq_stop"]->setScriptContents(templates.daqStop.contents);
            config->vmeScripts["readout_start"]->setScriptContents(templates.readoutCycleStart.contents);
            config->vmeScripts["readout_end"]->setScriptContents(templates.readoutCycleEnd.contents);
        }

        m_config->addEventConfig(config);

        if (auto node = m_treeMap.value(config, nullptr))
            node->setExpanded(true);
    }
    else
    {
        delete config;
    }
}

void VMEConfigTreeWidget::removeEvent()
{
    auto node = m_tree->currentItem();

    if (node && node->type() == NodeType_Event)
    {
        auto event = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
        m_config->removeEventConfig(event);
        event->deleteLater();
    }
}

void VMEConfigTreeWidget::editEvent()
{
    auto node = m_tree->currentItem();

    if (node && node->type() == NodeType_Event)
    {
        auto eventConfig = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
        EventConfigDialog dialog(m_context, m_context->getVMEController(), eventConfig, this);
        dialog.setWindowTitle(QSL("Edit Event"));
        dialog.exec();
    }
}

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
        bool doExpand = (event->modules.size() == 0);

        auto module = std::make_unique<ModuleConfig>();
        ModuleConfigDialog dialog(m_context, module.get(), this);
        dialog.setWindowTitle(QSL("Add Module"));
        int result = dialog.exec();

        if (result == QDialog::Accepted)
        {
            // Create and add script configs using the data stored in the
            // module meta information.
            auto moduleMeta = module->getModuleMeta();

            module->getReadoutScript()->setObjectName(moduleMeta.templates.readout.name);
            module->getReadoutScript()->setScriptContents(moduleMeta.templates.readout.contents);

            module->getResetScript()->setObjectName(moduleMeta.templates.reset.name);
            module->getResetScript()->setScriptContents(moduleMeta.templates.reset.contents);

            for (const auto &vmeTemplate: moduleMeta.templates.init)
            {
                module->addInitScript(new VMEScriptConfig(
                        vmeTemplate.name, vmeTemplate.contents));
            }

            event->addModuleConfig(module.release());

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
        ModuleConfigDialog dialog(m_context, moduleConfig, this);
        dialog.setWindowTitle(QSL("Edit Module"));
        dialog.exec();
    }
}

void VMEConfigTreeWidget::addGlobalScript()
{
    auto node = m_tree->currentItem();
    auto category = node->data(0, DataRole_ScriptCategory).toString();
    auto script = new VMEScriptConfig;

    script->setObjectName("new vme script");
    bool doExpand = (node->childCount() == 0);
    m_config->addGlobalScript(script, category);
    if (doExpand)
        node->setExpanded(true);

    auto scriptNode = m_treeMap.value(script, nullptr);
    if (scriptNode)
    {
        m_tree->editItem(scriptNode, 0);
    }
}

void VMEConfigTreeWidget::removeGlobalScript()
{
    auto node = m_tree->currentItem();
    auto script = Var2Ptr<VMEScriptConfig>(node->data(0, DataRole_Pointer));
    m_config->removeGlobalScript(script);
}

void VMEConfigTreeWidget::runScripts()
{
    auto node = m_tree->currentItem();
    auto obj  = Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer));

    QVector<VMEScriptConfig *> scriptConfigs;

    auto scriptConfig = qobject_cast<VMEScriptConfig *>(obj);

    if (scriptConfig)
    {
        scriptConfigs.push_back(scriptConfig);
    }
    else
    {
        for (int i=0; i<node->childCount(); ++i)
        {
            obj = Var2Ptr<ConfigObject>(node->child(i)->data(0, DataRole_Pointer));
            scriptConfig = qobject_cast<VMEScriptConfig *>(obj);
            scriptConfigs.push_back(scriptConfig);
        }
    }

    runScriptConfigs(scriptConfigs);
}

void VMEConfigTreeWidget::editName()
{
    m_tree->editItem(m_tree->currentItem(), 0);
}

void VMEConfigTreeWidget::initModule()
{
    auto node = m_tree->currentItem();
    auto module = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    runScriptConfigs(module->getInitScripts());
}

void VMEConfigTreeWidget::runScriptConfigs(const QVector<VMEScriptConfig *> &scriptConfigs)
{
    for (auto scriptConfig: scriptConfigs)
    {
        auto moduleConfig = qobject_cast<ModuleConfig *>(scriptConfig->parent());

        m_context->logMessage(QSL("Running script ") + scriptConfig->getVerboseTitle());

        try
        {
            auto logger = [this](const QString &str) { m_context->logMessage(QSL("  ") + str); };

            auto results = m_context->runScript(
                scriptConfig->getScript(moduleConfig ? moduleConfig->getBaseAddress() : 0),
                logger);

            for (auto result: results)
                logger(format_result(result));
        }
        catch (const vme_script::ParseError &e)
        {
            m_context->logMessage(QSL("Parse error: ") + e.what());
        }
    }
}


void VMEConfigTreeWidget::onActionShowAdvancedChanged()
{
    auto nodes = findItems(m_nodeEvents, [](QTreeWidgetItem *node) {
        return node->type() == NodeType_EventReadoutLoop
            || node->type() == NodeType_EventStartStop
            || node->type() == NodeType_ModuleReset;
    });

    nodes.push_back(m_nodeScripts);

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

void VMEConfigTreeWidget::dumpVMUSBRegisters()
{
    auto vmusb = dynamic_cast<VMUSB *>(m_context->getVMEController());

    if (vmusb && m_context->getDAQState() == DAQState::Idle)
    {
        dump_registers(vmusb, [this] (const QString &line) { m_context->logMessage(line); });
    }
}

void VMEConfigTreeWidget::showEditNotes()
{
}

void VMEConfigTreeWidget::updateConfigLabel()
{
    QString fileName = m_context->getConfigFileName();

    if (fileName.isEmpty())
        fileName = QSL("<not saved>");

    if (m_context->getVMEConfig()->isModified())
        fileName += QSL(" *");

    auto wsDir = m_context->getWorkspaceDirectory() + '/';

    if (fileName.startsWith(wsDir))
    {
        fileName.remove(wsDir);
    }

    le_fileName->setText(fileName);
    le_fileName->setToolTip(fileName);
    le_fileName->setStatusTip(fileName);
}
