#include "daqconfig_tree.h"
#include "mvme_config.h"
#include <QTreeWidget>
#include <QHBoxLayout>
#include <QDebug>

class TreeNode: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;
};

DAQConfigTreeWidget::DAQConfigTreeWidget(QWidget *parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
    , m_nodeEvents(new TreeNode)
    , m_nodeManual(new TreeNode)
    , m_nodeStart(new TreeNode)
    , m_nodeEnd(new TreeNode)
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
    m_nodeManual->setText(0,        QSL("manual"));
    m_nodeStart->setText(0,         QSL("daq_start"));
    m_nodeEnd->setText(0,           QSL("daq_end"));

    m_tree->addTopLevelItem(m_nodeEvents);
    m_tree->addTopLevelItem(m_nodeScripts);

    m_nodeScripts->addChild(m_nodeManual);
    m_nodeScripts->addChild(m_nodeStart);
    m_nodeScripts->addChild(m_nodeEnd);

    auto nodes = QList<TreeNode *>({ m_nodeEvents, m_nodeScripts });
    for (auto node: nodes)
        node->setExpanded(true);

    m_tree->resizeColumnToContents(0);

    auto layout = new QHBoxLayout(this);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &DAQConfigTreeWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &DAQConfigTreeWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged, this, &DAQConfigTreeWidget::onItemChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &DAQConfigTreeWidget::onItemExpanded);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &DAQConfigTreeWidget::treeContextMenu);
}

void DAQConfigTreeWidget::setConfig(DAQConfig *cfg)
{
    qDeleteAll(m_treeMap.values());
    m_treeMap.clear();

    m_config = cfg;

    if (cfg)
    {
        addScriptNodes(m_nodeManual, cfg->vmeScriptLists["manual"]);
        addScriptNodes(m_nodeStart, cfg->vmeScriptLists["daq_start"], true);
        addScriptNodes(m_nodeEnd, cfg->vmeScriptLists["daq_end"], true);


        for (auto event: cfg->eventConfigs)
            onEventAdded(event);
        //addEventNodes(m_nodeEvents, cfg->eventConfigs);

        connect(cfg, &DAQConfig::eventAdded, this, &DAQConfigTreeWidget::onEventAdded);
        connect(cfg, &DAQConfig::eventAboutToBeRemoved, this, &DAQConfigTreeWidget::onEventAboutToBeRemoved);
    }

    QTreeWidgetItemIterator it(m_tree);

    m_tree->resizeColumnToContents(0);
}

DAQConfig *DAQConfigTreeWidget::getConfig() const
{
    return m_config;
}

template<typename T>
TreeNode *makeNode(T *data)
{
    auto ret = new TreeNode;
    ret->setData(0, Qt::UserRole, Ptr2Var(data));
    return ret;
}

void DAQConfigTreeWidget::addScriptNodes(TreeNode *parent, QList<VMEScriptConfig *> scripts, bool canDisable)
{
    for (auto script: scripts)
    {
        if (!m_treeMap.contains(script))
        {
            auto node = makeNode(script);
            node->setText(0, script->objectName());
            node->setIcon(0, QIcon(":/vme_script.png"));
            if (canDisable)
            {
                node->setCheckState(0, script->isEnabled() ? Qt::Checked : Qt::Unchecked);
            }
            m_treeMap[script] = node;
            parent->addChild(node);
        }
    }
}

void DAQConfigTreeWidget::addEventNodes(TreeNode *parent, QList<EventConfig *> events)
{
    for (auto event: events)
    {
        auto eventNode = m_treeMap.value(event, nullptr);
        if (!eventNode)
        {
            eventNode = addEventNode(parent, event);
        }

        for (auto module: event->modules)
        {
            auto moduleNode = m_treeMap.value(module, nullptr);
            if (!moduleNode)
            {
            }
        }
    }
}

enum NodeType
{
    NodeType_Event = QTreeWidgetItem::UserType
};

TreeNode *DAQConfigTreeWidget::addEventNode(TreeNode *parent, EventConfig *event)
{
    auto eventNode = makeNode(event);
    eventNode->setText(0, event->objectName());
    eventNode->setCheckState(0, Qt::Checked);
    m_treeMap[event] = eventNode;
    parent->addChild(eventNode);

    auto modulesNode = new TreeNode;
    modulesNode->setText(0, QSL("Modules Init"));
    modulesNode->setIcon(0, QIcon(":/config_category.png"));
    eventNode->addChild(modulesNode);

    for (auto module: event->modules)
    {
        auto moduleNode = m_treeMap.value(module, nullptr);
        if (!moduleNode)
        {
            addModuleNode(modulesNode, module);
        }
    }

    auto readoutLoopNode = new TreeNode;
    readoutLoopNode->setText(0, QSL("Readout Loop"));
    readoutLoopNode->setIcon(0, QIcon(":/config_category.png"));
    eventNode->addChild(readoutLoopNode);

    {
        auto node = makeNode(event->vmeScripts["readout_start"]);
        node->setText(0, QSL("Loop Start"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        readoutLoopNode->addChild(node);
    }

    for (auto module: event->modules)
    {
        auto moduleReadoutNode = new TreeNode;
        moduleReadoutNode->setText(0, module->objectName());
        moduleReadoutNode->setIcon(0, QIcon(":/vme_module.png"));
        readoutLoopNode->addChild(moduleReadoutNode);
    }

    {
        auto node = makeNode(event->vmeScripts["readout_end"]);
        node->setText(0, QSL("Loop End"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        readoutLoopNode->addChild(node);
    }

    {
        auto node = makeNode(event->vmeScripts["daq_start"]);
        node->setText(0, QSL("DAQ Start"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        eventNode->addChild(node);
    }

    {
        auto node = makeNode(event->vmeScripts["daq_end"]);
        node->setText(0, QSL("DAQ End"));
        node->setIcon(0, QIcon(":/vme_script.png"));
        eventNode->addChild(node);
    }

    return eventNode;
}

TreeNode *DAQConfigTreeWidget::addModuleNode(TreeNode *parent, ModuleConfig *module)
{
    auto moduleNode = makeNode(module);
    moduleNode->setText(0, module->objectName());
    moduleNode->setCheckState(0, Qt::Checked);
    moduleNode->setIcon(0, QIcon(":/vme_module.png"));
    m_treeMap[module] = moduleNode;
    parent->addChild(moduleNode);

    auto parametersNode = makeNode(module->vmeScripts["parameters"]);
    parametersNode->setText(0, QSL("Module Init"));
    parametersNode->setIcon(0, QIcon(":/vme_script.png"));
    moduleNode->addChild(parametersNode);

    auto readoutSettingsNode = makeNode(module->vmeScripts["readout_settings"]);
    readoutSettingsNode->setText(0, QSL("Readout Settings"));
    readoutSettingsNode->setIcon(0, QIcon(":/vme_script.png"));
    moduleNode->addChild(readoutSettingsNode);

    return moduleNode;
}

void DAQConfigTreeWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << "clicked" << item << Var2Ptr<QObject>(item->data(0, Qt::UserRole)) << column;
}

void DAQConfigTreeWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    qDebug() << "doubleClicked" << item << Var2Ptr<QObject>(item->data(0, Qt::UserRole)) << column;
}

void DAQConfigTreeWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    qDebug() << "changed" << item << Var2Ptr<QObject>(item->data(0, Qt::UserRole)) << column;
}

void DAQConfigTreeWidget::onItemExpanded(QTreeWidgetItem *item)
{
    m_tree->resizeColumnToContents(0);
}

void DAQConfigTreeWidget::treeContextMenu(const QPoint &pos)
{
    auto item = m_tree->itemAt(pos);

    qDebug() << "context menu for" << item;
}

void DAQConfigTreeWidget::onEventAdded(EventConfig *config)
{
    addEventNode(m_nodeEvents, config);

    connect(config, &EventConfig::moduleAdded, this, &DAQConfigTreeWidget::onModuleAdded);
    connect(config, &EventConfig::moduleAboutToBeRemoved, this, &DAQConfigTreeWidget::onModuleAboutToBeRemoved);
}

void DAQConfigTreeWidget::onEventAboutToBeRemoved(EventConfig *config)
{
    // TODO: remove config and its subobjects from the treemap!
    delete m_treeMap[config];
}

void DAQConfigTreeWidget::onModuleAdded(ModuleConfig *config)
{
    // TODO: get the "Modules Init" node!
    auto parentNode = m_treeMap[config->parent()];
    addModuleNode(parentNode, config);
}

void DAQConfigTreeWidget::onModuleAboutToBeRemoved(ModuleConfig *config)
{
    delete m_treeMap[config];
}
