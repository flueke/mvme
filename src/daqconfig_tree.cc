#include "daqconfig_tree.h"
#include "mvme_config.h"
#include "mvme_context.h"
#include "config_widgets.h"
#include <QTreeWidget>
#include <QHBoxLayout>
#include <QDebug>
#include <QMenu>

enum NodeType
{
    NodeType_Event = QTreeWidgetItem::UserType,
    NodeType_Module,
    NodeType_EventModulesInit,
    NodeType_GlobalScript
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_ScriptCategory,
    DataRole_CanDisableChildScripts
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

DAQConfigTreeWidget::DAQConfigTreeWidget(MVMEContext *context, QWidget *parent)
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
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(10);

    auto headerItem = m_tree->headerItem();
    headerItem->setText(0, QSL("Object"));
    headerItem->setText(1, QSL("Info"));


    m_nodeEvents->setText(0,        QSL("Events"));
    m_nodeScripts->setText(0,       QSL("Global Scripts"));

    m_nodeStart->setText(0, QSL("DAQ Start"));
    m_nodeStart->setData(0, DataRole_ScriptCategory, "daq_start");
    m_nodeStart->setData(0, DataRole_CanDisableChildScripts, true);

    m_nodeStop->setText(0, QSL("DAQ Stop"));
    m_nodeStop->setData(0, DataRole_ScriptCategory, "daq_stop");
    m_nodeStop->setData(0, DataRole_CanDisableChildScripts, true);

    m_nodeManual->setText(0,  QSL("Manual"));
    m_nodeManual->setData(0,  DataRole_ScriptCategory, "manual");
    m_nodeManual->setData(0,  DataRole_CanDisableChildScripts, false);

    m_tree->addTopLevelItem(m_nodeEvents);
    m_tree->addTopLevelItem(m_nodeScripts);

    m_nodeScripts->addChild(m_nodeStart);
    m_nodeScripts->addChild(m_nodeStop);
    m_nodeScripts->addChild(m_nodeManual);

    auto nodes = QList<TreeNode *>({ m_nodeEvents, m_nodeScripts });
    for (auto node: nodes)
        node->setExpanded(true);

    m_tree->resizeColumnToContents(0);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &DAQConfigTreeWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &DAQConfigTreeWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged, this, &DAQConfigTreeWidget::onItemChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &DAQConfigTreeWidget::onItemExpanded);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &DAQConfigTreeWidget::treeContextMenu);
}

void DAQConfigTreeWidget::setConfig(DAQConfig *cfg)
{
    qDebug() << __PRETTY_FUNCTION__ << "deleting" << m_treeMap.size() << "nodes";
    for (auto it=m_treeMap.begin(); it!=m_treeMap.end(); ++it)
    {
        qDebug() << "\t" << &(it.key()) << it.value();
    }

    qDeleteAll(m_treeMap.values());
    m_treeMap.clear();

    m_config = cfg;

    if (cfg)
    {
        for (auto category: cfg->vmeScriptLists.keys())
            for (auto script: cfg->vmeScriptLists[category])
                onScriptAdded(script, category);

        for (auto event: cfg->eventConfigs)
            onEventAdded(event);

        connect(cfg, &DAQConfig::eventAdded, this, &DAQConfigTreeWidget::onEventAdded);
        connect(cfg, &DAQConfig::eventAboutToBeRemoved, this, &DAQConfigTreeWidget::onEventAboutToBeRemoved);
        connect(cfg, &DAQConfig::globalScriptAdded, this, &DAQConfigTreeWidget::onScriptAdded);
        connect(cfg, &DAQConfig::globalScriptAboutToBeRemoved, this, &DAQConfigTreeWidget::onScriptAboutToBeRemoved);
    }

    m_tree->resizeColumnToContents(0);
}

DAQConfig *DAQConfigTreeWidget::getConfig() const
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

TreeNode *DAQConfigTreeWidget::addScriptNode(TreeNode *parent, VMEScriptConfig* script, bool canDisable)
{
    auto node = new TreeNode(NodeType_GlobalScript);
    node->setData(0, DataRole_Pointer, Ptr2Var(script));
    node->setText(0, script->objectName());
    node->setIcon(0, QIcon(":/vme_script.png"));
    if (canDisable)
    {
        node->setCheckState(0, script->isEnabled() ? Qt::Checked : Qt::Unchecked);
    }
    m_treeMap[script] = node;
    parent->addChild(node);

    return node;
}

TreeNode *DAQConfigTreeWidget::addEventNode(TreeNode *parent, EventConfig *event)
{
    auto eventNode = new EventNode;
    eventNode->setData(0, DataRole_Pointer, Ptr2Var(event));
    eventNode->setText(0, event->objectName());
    eventNode->setCheckState(0, Qt::Checked);
    m_treeMap[event] = eventNode;
    parent->addChild(eventNode);

    eventNode->modulesNode = new TreeNode(NodeType_EventModulesInit);
    auto modulesNode = eventNode->modulesNode;
    modulesNode->setText(0, QSL("Modules Init"));
    modulesNode->setIcon(0, QIcon(":/config_category.png"));
    eventNode->addChild(modulesNode);

    eventNode->readoutLoopNode = new TreeNode;
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

    eventNode->daqStartStopNode = new TreeNode;
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

    for (auto module: event->modules)
    {
        auto moduleNode = m_treeMap.value(module, nullptr);
        if (!moduleNode)
        {
            addModuleNodes(eventNode, module);
        }
    }

    return eventNode;
}

TreeNode *DAQConfigTreeWidget::addModuleNodes(EventNode *parent, ModuleConfig *module)
{
    auto moduleNode = new ModuleNode;
    moduleNode->setData(0, DataRole_Pointer, Ptr2Var(module));
    moduleNode->setText(0, module->objectName());
    moduleNode->setCheckState(0, Qt::Checked);
    moduleNode->setIcon(0, QIcon(":/vme_module.png"));
    m_treeMap[module] = moduleNode;
    parent->modulesNode->addChild(moduleNode);

    auto parametersNode = makeNode(module->vmeScripts["parameters"]);
    parametersNode->setText(0, QSL("Module Init"));
    parametersNode->setIcon(0, QIcon(":/vme_script.png"));
    moduleNode->addChild(parametersNode);

    auto readoutSettingsNode = makeNode(module->vmeScripts["readout_settings"]);
    readoutSettingsNode->setText(0, QSL("VME Interface Settings"));
    readoutSettingsNode->setIcon(0, QIcon(":/vme_script.png"));
    moduleNode->addChild(readoutSettingsNode);

    auto readoutNode = makeNode(module->vmeScripts["readout"]);
    moduleNode->readoutNode = readoutNode;
    readoutNode->setText(0, module->objectName());
    readoutNode->setIcon(0, QIcon(":/vme_module.png"));

    auto readoutLoopNode = parent->readoutLoopNode;
    readoutLoopNode->insertChild(readoutLoopNode->childCount() - 1, readoutNode);

    return moduleNode;
}

void DAQConfigTreeWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << "clicked" << item << Var2Ptr<QObject>(item->data(0, DataRole_Pointer)) << column;
}

void DAQConfigTreeWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << "doubleClicked" << item << Var2Ptr<QObject>(item->data(0, DataRole_Pointer)) << column;
}

void DAQConfigTreeWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    qDebug() << "changed" << item << Var2Ptr<QObject>(item->data(0, DataRole_Pointer)) << column;

    switch (item->type())
    {
        case NodeType_GlobalScript:
            {
                auto obj = Var2Ptr<ConfigObject>(item->data(0, DataRole_Pointer));
                if (item->flags() & Qt::ItemIsUserCheckable)
                {
                    obj->setEnabled(item->checkState(0) != Qt::Unchecked);
                    obj->setObjectName(item->text(0));
                }
            } break;
    }
}

void DAQConfigTreeWidget::onItemExpanded(QTreeWidgetItem *item)
{
    m_tree->resizeColumnToContents(0);
}

void DAQConfigTreeWidget::treeContextMenu(const QPoint &pos)
{
    auto node = m_tree->itemAt(pos);

    qDebug() << "context menu for" << node;

    QMenu menu;

    //
    // Events
    //
    if (node == m_nodeEvents)
    {
        menu.addAction(QSL("Add Event"), this, &DAQConfigTreeWidget::addEvent);
    }

    if (node->type() == NodeType_Event)
    {
        menu.addAction(QSL("Add Module"), this, &DAQConfigTreeWidget::addModule);
        menu.addSeparator();
        menu.addAction(QSL("Remove Event"), this, &DAQConfigTreeWidget::removeEvent);
    }

    if (node->type() == NodeType_EventModulesInit)
    {
        menu.addAction(QSL("Add Module"), this, &DAQConfigTreeWidget::addModule);
    }

    if (node->type() == NodeType_Module)
    {
        menu.addAction(QSL("Remove Module"), this, &DAQConfigTreeWidget::removeModule);
    }

    //
    // Global scripts
    //
    if (node == m_nodeStart || node == m_nodeStop || node == m_nodeManual)
    {
        menu.addAction(QSL("Add script"), this, &DAQConfigTreeWidget::addGlobalScript);

        if (node->childCount() > 0)
            menu.addAction(QSL("Run scripts"), this, &DAQConfigTreeWidget::runScripts);
    }

    auto parent = node->parent();

    if (parent == m_nodeStart || parent == m_nodeStop || parent == m_nodeManual)
    {
        menu.addAction(QSL("Run Script"), this, &DAQConfigTreeWidget::runScripts);
        menu.addSeparator();
        menu.addAction(QSL("Remove Script"), this, &DAQConfigTreeWidget::removeGlobalScript);
    }

    if (!menu.isEmpty())
    {
        menu.exec(m_tree->mapToGlobal(pos));
    }
}

void DAQConfigTreeWidget::onEventAdded(EventConfig *config)
{
    addEventNode(m_nodeEvents, config);

    connect(config, &EventConfig::moduleAdded, this, &DAQConfigTreeWidget::onModuleAdded);
    connect(config, &EventConfig::moduleAboutToBeRemoved, this, &DAQConfigTreeWidget::onModuleAboutToBeRemoved);
}

void DAQConfigTreeWidget::onEventAboutToBeRemoved(EventConfig *config)
{
    for (auto module: config->modules)
    {
        onModuleAboutToBeRemoved(module);
    }

    delete m_treeMap.take(config);
}

void DAQConfigTreeWidget::onModuleAdded(ModuleConfig *config)
{
    auto eventNode = static_cast<EventNode *>(m_treeMap[config->parent()]);
    addModuleNodes(eventNode, config);
}

void DAQConfigTreeWidget::onModuleAboutToBeRemoved(ModuleConfig *config)
{
    auto moduleNode = static_cast<ModuleNode *>(m_treeMap[config]);
    delete moduleNode->readoutNode;
    delete m_treeMap.take(config);
}

void DAQConfigTreeWidget::onScriptAdded(VMEScriptConfig *script, const QString &category)
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
    }
}

void DAQConfigTreeWidget::onScriptAboutToBeRemoved(VMEScriptConfig *script)
{
    delete m_treeMap.take(script);
}

//
// context menu action implementations
//
void DAQConfigTreeWidget::addEvent()
{
    auto config = new EventConfig;
    config->setObjectName(QString("event%1").arg(m_config->getEventConfigs().size()));
    EventConfigDialog dialog(m_context, config);
    int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        m_config->addEventConfig(config);
    }
    else
    {
        delete config;
    }
}

void DAQConfigTreeWidget::removeEvent()
{
    auto node = m_tree->currentItem();

    if (node && node->type() == NodeType_Event)
    {
        auto event = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
        m_config->removeEventConfig(event);
        delete event;
    }
}

void DAQConfigTreeWidget::addModule()
{
    auto node = m_tree->currentItem();

    while (node && node->type() != NodeType_Event)
    {
        node = node->parent();
    }

    if (node)
    {
        auto event = Var2Ptr<EventConfig>(node->data(0, DataRole_Pointer));
        AddModuleDialog dialog(m_context, event);
        dialog.exec();
    }
}

void DAQConfigTreeWidget::removeModule()
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
            delete module;
        }
    }
}

void DAQConfigTreeWidget::addGlobalScript()
{
    auto node = m_tree->currentItem();
    auto category = node->data(0, DataRole_ScriptCategory).toString();
    auto script = new VMEScriptConfig;
    script->setObjectName("vme script");
    qDebug() << __PRETTY_FUNCTION__ << script << category;
    m_config->addGlobalScript(script, category);
}

void DAQConfigTreeWidget::removeGlobalScript()
{
    auto node = m_tree->currentItem();
    auto script = Var2Ptr<VMEScriptConfig>(node->data(0, DataRole_Pointer));
    m_config->removeGlobalScript(script);
}

void DAQConfigTreeWidget::runScripts()
{
    auto node = m_tree->currentItem();

    qDebug() << __PRETTY_FUNCTION__ << node;
}
