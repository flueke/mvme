#include "histogram_tree.h"
#include "histogram.h"
#include "hist1d.h"
#include "hist2d.h"
#include "hist2ddialog.h"
#include "mvme_context.h"
#include "mvme_config.h"
#include "treewidget_utils.h"
#include "config_widgets.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTimer>
#include <QSettings>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>

//
// Utility functions for filter and histogram creation.
//
using DataFilterConfigList = AnalysisConfig::DataFilterConfigList;

static DataFilterConfigList generateDefaultFilters(ModuleConfig *moduleConfig)
{
    DataFilterConfigList result;

    const auto filterDefinitions = defaultDataFilters.value(moduleConfig->type);

    for (const auto &def: filterDefinitions)
    {
        auto cfg = new DataFilterConfig(QByteArray(def.filter));
        cfg->setObjectName(def.name);
        result.push_back(cfg);
    }

    return result;
}

static QList<Hist1DConfig *> generateHistogramConfigs(DataFilterConfig *filterConfig)
{
    QList<Hist1DConfig *> result;

    const auto &filter = filterConfig->getFilter();
    u32 addressCount = 1 << filter.getExtractBits('A');
    u32 dataBits = filter.getExtractBits('D');

    for (u32 address = 0; address < addressCount; ++address)
    {
        auto cfg = new Hist1DConfig;
        cfg->setObjectName(QString::number(address));
        cfg->setFilterId(filterConfig->getId());
        cfg->setFilterAddress(address);
        cfg->setBits(dataBits);
        cfg->setProperty("xAxisTitle", QString("%1 %2").arg(filterConfig->objectName()).arg(address));
        result.push_back(cfg);
    }

    return result;
}

static Hist1D *createHistogram(Hist1DConfig *config)
{
    Hist1D *result = new Hist1D(config->getBits());
    result->setProperty("configId", config->getId());
    return result;
}

static Hist1D *createAndAddHist1D(MVMEContext *context, Hist1DConfig *histoConfig)
{
    context->getAnalysisConfig()->addHist1DConfig(histoConfig);
    auto histo = createHistogram(histoConfig);
    histo->setParent(context);
    context->addObjectMapping(histoConfig, histo, QSL("ConfigToObject"));
    context->addObjectMapping(histo, histoConfig, QSL("ObjectToConfig"));
    context->addObject(histo);
    return histo;
}

//
// Histo Tree stuff
//
enum NodeType
{
    NodeType_Module = QTreeWidgetItem::UserType,
    NodeType_Hist1D,
    NodeType_Hist2D,
    NodeType_DataFilter,
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_FilterAddress,
};

class TreeNode: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;
};

template<typename T>
TreeNode *makeNode(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_Pointer, Ptr2Var(data));
    return ret;
}

static QList<QPair<TreeNode *, Hist1D *>>
generateHistogramNodes(MVMEContext *context, DataFilterConfig *filterConfig)
{
    QList<QPair<TreeNode *, Hist1D *>> result;

    const auto &filter = filterConfig->getFilter();
    u32 addressCount = 1 << filter.getExtractBits('A');

    for (u32 address = 0; address < addressCount; ++address)
    {
        auto predicate = [filterConfig, address] (Hist1DConfig *histoConfig)
        {
            return (histoConfig->getFilterId() == filterConfig->getId()
                    && histoConfig->getFilterAddress() == address);
        };

        auto histoConfig = context->getAnalysisConfig()->findChildByPredicate<Hist1DConfig *>(predicate);
        auto histo = qobject_cast<Hist1D *>(context->getMappedObject(histoConfig, QSL("ConfigToObject")));

        Q_ASSERT(histoConfig);
        Q_ASSERT(histo);

        auto addressNode = makeNode(histo, NodeType_Hist1D);
        addressNode->setText(0, QString::number(address));
        addressNode->setIcon(0, QIcon(":/hist1d.png"));
        addressNode->setData(0, DataRole_FilterAddress, address);

        result.push_back(qMakePair(addressNode, histo));
    }

    return result;
}

HistogramTreeWidget::HistogramTreeWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_tree(new QTreeWidget)
    , m_node1D(new TreeNode)
    , m_node2D(new TreeNode)
{
    m_tree->setColumnCount(2);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(10);
    m_tree->setItemDelegateForColumn(1, new NoEditDelegate(this));
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);

    auto headerItem = m_tree->headerItem();
    headerItem->setText(0, QSL("Object"));
    headerItem->setText(1, QSL("Info"));

    m_node1D->setText(0, QSL("1D"));
    m_node2D->setText(0, QSL("2D"));

    m_tree->addTopLevelItem(m_node1D);
    m_tree->addTopLevelItem(m_node2D);

    m_node1D->setExpanded(true);
    m_node2D->setExpanded(true);

    // buttons
    pb_load = new QPushButton(QIcon(":/document-open.png"), QSL(""));
    pb_save = new QPushButton(QIcon(":/document-save.png"), QSL(""));
    pb_saveAs = new QPushButton(QIcon(":/document-save-as.png"), QSL(""));

    connect(pb_load, &QPushButton::clicked, this, &HistogramTreeWidget::loadConfig);
    connect(pb_save, &QPushButton::clicked, this, &HistogramTreeWidget::saveConfig);
    connect(pb_saveAs, &QPushButton::clicked, this, &HistogramTreeWidget::saveConfigAs);

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(pb_load);
    buttonLayout->addWidget(pb_save);
    buttonLayout->addWidget(pb_saveAs);
    buttonLayout->addStretch(1);

    // widget layout
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttonLayout);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked, this, &HistogramTreeWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &HistogramTreeWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged, this, &HistogramTreeWidget::onItemChanged);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &HistogramTreeWidget::onItemExpanded);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &HistogramTreeWidget::treeContextMenu);

    connect(m_context, &MVMEContext::objectAdded, this, &HistogramTreeWidget::onObjectAdded);
    connect(m_context, &MVMEContext::objectAboutToBeRemoved, this, &HistogramTreeWidget::onObjectAboutToBeRemoved);

    connect(m_context, &MVMEContext::daqConfigChanged, this, &HistogramTreeWidget::onAnyConfigChanged);
    connect(m_context, &MVMEContext::analysisConfigChanged, this, &HistogramTreeWidget::onAnyConfigChanged);

    onAnyConfigChanged();

    auto timer = new QTimer(this);
    timer->setInterval(1000);
    timer->start();
    connect(timer, &QTimer::timeout, this, &HistogramTreeWidget::updateHistogramCountDisplay);
}

void HistogramTreeWidget::onObjectAdded(QObject *object)
{
    qDebug() << __PRETTY_FUNCTION__ << object;

    if (auto eventConfig = qobject_cast<EventConfig *>(object))
    {
        connect(eventConfig, &EventConfig::moduleAdded, this, &HistogramTreeWidget::onObjectAdded);
        connect(eventConfig, &EventConfig::moduleAboutToBeRemoved, this, &HistogramTreeWidget::onObjectAboutToBeRemoved);

        for (auto moduleConfig: eventConfig->getModuleConfigs())
            onObjectAdded(moduleConfig);

    }
    else if (auto moduleConfig = qobject_cast<ModuleConfig *>(object))
    {
        auto moduleNode = makeNode(moduleConfig, NodeType_Module);
        moduleNode->setText(0, moduleConfig->objectName());
        moduleNode->setIcon(0, QIcon(":/vme_module.png"));
        addToTreeMap(moduleConfig, moduleNode);
        m_node1D->addChild(moduleNode);

        auto idxPair = m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig);

        for (auto filterConfig: m_context->getAnalysisConfig()->getFilters(idxPair.first, idxPair.second))
        {
            onObjectAdded(filterConfig);
        }
    }
    else if (auto filterConfig = qobject_cast<DataFilterConfig *>(object))
    {
        auto idxPair = m_context->getAnalysisConfig()->getEventAndModuleIndices(filterConfig);
        if (idxPair.first < 0)
        {
            qDebug() << __PRETTY_FUNCTION__ << "!!! invalid analysisconfig indices for filterConfig" << filterConfig;
            return;
        }
        auto moduleConfig = m_context->getDAQConfig()->getModuleConfig(idxPair.first, idxPair.second);
        auto moduleNode = m_treeMap.value(moduleConfig);

        if (moduleNode)
        {
            auto filterNode = makeNode(filterConfig, NodeType_DataFilter);
            filterNode->setText(0, filterConfig->objectName());
            filterNode->setIcon(0, QIcon(":/data_filter.png"));
            moduleNode->addChild(filterNode);
            addToTreeMap(filterConfig, filterNode);

            auto histoNodePairs = generateHistogramNodes(m_context, filterConfig);

            for (auto pair: histoNodePairs)
            {
                filterNode->addChild(pair.first);
                addToTreeMap(pair.second, pair.first);
            }
        }
        else
        {
            qDebug() << __PRETTY_FUNCTION__ << "!!! no module node found for filter config" << filterConfig << "and module config" << moduleConfig;
        }
    }
    else if (auto histoConfig = qobject_cast<Hist2D *>(object))
    {
        auto node = makeNode(object, NodeType_Hist2D);
        node->setText(0, object->objectName());
        m_treeMap[object] = node;
        m_node2D->addChild(node);
        m_tree->resizeColumnToContents(0);
    }
}

void HistogramTreeWidget::onObjectAboutToBeRemoved(QObject *object)
{
    qDebug() << __PRETTY_FUNCTION__ << object;
    auto node = m_treeMap.value(object, nullptr);
    removeFromTreeMap(object);
    delete node;
}

void HistogramTreeWidget::onAnyConfigChanged()
{
    bool daqChanged = m_daqConfig != m_context->getDAQConfig();
    bool analysisChanged = m_analysisConfig != m_context->getAnalysisConfig();

    m_daqConfig = m_context->getDAQConfig();
    m_analysisConfig = m_context->getAnalysisConfig();

    qDeleteAll(m_node1D->takeChildren());
    qDeleteAll(m_node2D->takeChildren());
    m_treeMap.clear();

    if (m_daqConfig)
    {
        for (auto eventConfig: m_daqConfig->getEventConfigs())
            onObjectAdded(eventConfig);

        connect(m_daqConfig, &DAQConfig::eventAdded, this, &HistogramTreeWidget::onObjectAdded);
    }

    if (m_analysisConfig)
    {
       auto filters = m_analysisConfig->getFilters();

       for (int eventIndex: filters.keys())
       {
           for (int moduleIndex: filters[eventIndex].keys())
           {
               for (auto filter: filters[eventIndex][moduleIndex])
                   onObjectAdded(filter);
           }
       }

       //connect(m_analysisConfig, &AnalysisConfig::objectAdded, this, &HistogramTreeWidget::onObjectAdded);
       //connect(m_analysisConfig
    }
       // TODO: where and when does histogram generation happen?
}

void HistogramTreeWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    auto obj = Var2Ptr<QObject>(item->data(0, DataRole_Pointer));

    qDebug() << __PRETTY_FUNCTION__ << item << obj;

    if (obj)
        emit objectClicked(obj);
}

void HistogramTreeWidget::onItemDoubleClicked(QTreeWidgetItem *node, int column)
{
    auto obj = Var2Ptr<QObject>(node->data(0, DataRole_Pointer));
    qDebug() << __PRETTY_FUNCTION__ << node << obj;

    switch (node->type())
    {
        case NodeType_Hist1D:
            {
                emit objectDoubleClicked(obj);
            } break;
        case NodeType_DataFilter:
            {
                auto moduleNode   = node->parent();
                auto moduleConfig = Var2Ptr<ModuleConfig>(moduleNode->data(0, DataRole_Pointer));
                auto defaultFilter = QString(defaultDataFilters.value(moduleConfig->type).value(0).filter);
                auto filterConfig = Var2Ptr<DataFilterConfig>(node->data(0, DataRole_Pointer));

                DataFilterDialog dialog(filterConfig, defaultFilter);

                if (dialog.exec() == QDialog::Accepted)
                {
                    qDebug() << "<<<<< begin edited filter";
                    // remove all the histograms (config and histogram) that where generated by this filter.
                    auto histoNodes = node->takeChildren();

                    for (auto histoNode: histoNodes)
                    {
                        removeHist1D(histoNode);
                    }

                    // generate new histograms from the filter
                    for (auto histoConfig: generateHistogramConfigs(filterConfig))
                    {
                        createAndAddHist1D(m_context, histoConfig);
                    }

                    // generate the histogram nodes
                    auto histoNodePairs = generateHistogramNodes(m_context, filterConfig);

                    for (auto pair: histoNodePairs)
                    {
                        node->addChild(pair.first);
                        addToTreeMap(pair.second, pair.first);
                    }
                    qDebug() << "<<<<< end edited filter";
                }
            } break;
    }
}

void HistogramTreeWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
}

void HistogramTreeWidget::onItemExpanded(QTreeWidgetItem *item)
{
    m_tree->resizeColumnToContents(0);
}

void HistogramTreeWidget::treeContextMenu(const QPoint &pos)
{
    auto node = m_tree->itemAt(pos);
    auto parent = node->parent();
    auto obj = Var2Ptr<QObject>(node->data(0, DataRole_Pointer));

    QMenu menu;

    if (node == m_node1D)
    {
        menu.addAction(QSL("Clear Histograms"), this, &HistogramTreeWidget::clearHistograms);
    }

    if (node->type() == NodeType_Module)
    {
        menu.addAction(QSL("Clear Histograms"), this, &HistogramTreeWidget::clearHistograms);
        menu.addAction(QSL("Add filter"), this, &HistogramTreeWidget::addDataFilter);
        menu.addAction(QSL("Generate default filters"), this, &HistogramTreeWidget::generateDefaultFilters);

    }

    if (node->type() == NodeType_DataFilter)
    {
        menu.addAction(QSL("Clear Histograms"), this, &HistogramTreeWidget::clearHistograms);
        menu.addSeparator();
        menu.addAction(QSL("Remove filter"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(
                       &HistogramTreeWidget::removeDataFilter));
    }

    if (node->type() == NodeType_Hist1D)
    {
        menu.addAction(QSL("Clear"), this, &HistogramTreeWidget::clearHistogram);
    }

#if 0
    if (node->type() == NodeType_HistoCollection
        || node->type() == NodeType_Histo2D)
    {
        menu.addAction(QSL("Open in new window"), this, [obj, this]() { emit openInNewWindow(obj); });
        menu.addAction(QSL("Clear"), this, &HistogramTreeWidget::clearHistogram);

    }
#endif

    if (node == m_node2D && m_context->getConfig()->getAllModuleConfigs().size())
    {
        menu.addAction(QSL("Add 2D Histogram"), this, &HistogramTreeWidget::add2DHistogram);
    }

    if (!menu.isEmpty())
    {
        menu.exec(m_tree->mapToGlobal(pos));
    }
}

void HistogramTreeWidget::clearHistogram()
{
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);

    if (auto histo = Var2QObject<Hist1D>(var))
        histo->clear();

    if (auto histo = Var2QObject<Hist2D>(var))
        histo->clear();
}

void HistogramTreeWidget::add2DHistogram()
{
    Hist2DDialog dialog(m_context);
    int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        auto histoAndConfig = dialog.getHistoAndConfig();
        auto histo = histoAndConfig.first;
        auto histoConfig = histoAndConfig.second;
        histo->setProperty("configId", histoConfig->getId());
        m_context->addObject(histo);
        m_context->addObjectMapping(histo, histoConfig, QSL("ObjectToConfig"));
        m_context->addObjectMapping(histoConfig, histo, QSL("ConfigToObject"));
        m_context->getAnalysisConfig()->addHist2DConfig(histoConfig);
        emit openInNewWindow(histo);
    }
}

void HistogramTreeWidget::updateHistogramCountDisplay()
{
    for (auto it = m_treeMap.begin();
         it != m_treeMap.end();
         ++it)
    {
        if (auto histo = qobject_cast<Hist1D *>(it.key()))
        {
            //qDebug() << __PRETTY_FUNCTION__ << histo << it.value();
            auto node = it.value();
            node->setText(1, QString("entries=%1").arg(histo->getEntryCount()));
        }
    }
}

void HistogramTreeWidget::generateDefaultFilters()
{
    qDebug() << __PRETTY_FUNCTION__;
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);
    auto moduleConfig = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    auto indices = m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig);

    if (indices.first < 0)
    {
        qDebug() << __PRETTY_FUNCTION__ << "invalid daqconfig indices for moduleConfig" << moduleConfig;
        return;
    }

    while (node->childCount() > 0)
    {
        auto filterNode = node->child(0);
        if (filterNode->type() == NodeType_DataFilter)
        {
            removeDataFilter(filterNode);
        }
    }

    auto filterConfigs = ::generateDefaultFilters(moduleConfig);

    qDebug() << __PRETTY_FUNCTION__ << "generated filters:" << filterConfigs;

    for (auto filterConfig: filterConfigs)
    {
        for (auto histoConfig: generateHistogramConfigs(filterConfig))
            createAndAddHist1D(m_context, histoConfig);
    }

    m_context->getAnalysisConfig()->setFilters(indices.first, indices.second, filterConfigs);
}

void HistogramTreeWidget::addDataFilter()
{
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);
    auto moduleConfig = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    auto defaultFilter = defaultDataFilters.value(moduleConfig->type).value(0).filter;
    auto filterConfig = new DataFilterConfig(DataFilter(defaultFilter));

    DataFilterDialog dialog(filterConfig, defaultFilter);

    if (dialog.exec() == QDialog::Accepted)
    {
        for (auto histoConfig: generateHistogramConfigs(filterConfig))
            createAndAddHist1D(m_context, histoConfig);

        auto indices = m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig);
        if (indices.first < 0)
        {
            qDebug() << __PRETTY_FUNCTION__ << "invalid daqconfig indices for moduleConfig" << moduleConfig;
            return;
        }
        m_context->getAnalysisConfig()->addFilter(indices.first, indices.second, filterConfig);
    }
    else
    {
        delete filterConfig;
    }
}

void HistogramTreeWidget::removeDataFilter()
{
    auto node = m_tree->currentItem();
    removeDataFilter(node);
}

void HistogramTreeWidget::removeDataFilter(QTreeWidgetItem *node)
{
    Q_ASSERT(node->type() == NodeType_DataFilter);
    auto filterConfig = Var2Ptr<DataFilterConfig>(node->data(0, DataRole_Pointer));

    Q_ASSERT(filterConfig);
    Q_ASSERT(m_treeMap[filterConfig] == node);

    for (auto histoNode: node->takeChildren())
    {
        removeHist1D(histoNode);
    }

    auto moduleNode   = node->parent();

    delete node;
    removeFromTreeMap(filterConfig);

    auto moduleConfig = Var2Ptr<ModuleConfig>(moduleNode->data(0, DataRole_Pointer));
    auto indices      = m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig);
    if (indices.first < 0)
    {
        qDebug() << __PRETTY_FUNCTION__ << "invalid daqconfig indices for moduleConfig" << moduleConfig;
        return;
    }
    m_context->getAnalysisConfig()->removeFilter(indices.first, indices.second, filterConfig);
}

void HistogramTreeWidget::removeHist1D(QTreeWidgetItem *node)
{
    Q_ASSERT(node->type() == NodeType_Hist1D);

    auto histo = Var2Ptr<Hist1D>(node->data(0, DataRole_Pointer));

    Q_ASSERT(histo);
    Q_ASSERT(m_treeMap[histo] == node);

    delete node;
    removeFromTreeMap(histo);

    auto histoConfig = qobject_cast<Hist1DConfig *>(
        m_context->removeObjectMapping(histo, QSL("ObjectToConfig")));

    m_context->removeObject(histo);
    m_context->getAnalysisConfig()->removeHist1DConfig(histoConfig);
}

void HistogramTreeWidget::clearHistograms()
{
    auto clearFilterNode = [](QTreeWidgetItem *filterNode)
    {
        for (int i = 0; i < filterNode->childCount(); ++i)
        {
            auto histoNode = filterNode->child(i);
            if (histoNode->type() == NodeType_Hist1D)
            {
                auto var = histoNode->data(0, DataRole_Pointer);
                if (auto histo = Var2QObject<Hist1D>(var))
                    histo->clear();
            }
        }
    };

    auto clearModuleNode = [clearFilterNode](QTreeWidgetItem *moduleNode)
    {
        for (int i = 0; i < moduleNode->childCount(); ++i)
        {
            auto filterNode = moduleNode->child(i);
            if (filterNode->type() == NodeType_DataFilter)
                clearFilterNode(filterNode);
        }
    };

    auto node = m_tree->currentItem();

    if (node->type() == NodeType_Module)
    {
        clearModuleNode(node);
    }
    else if (node->type() == NodeType_DataFilter)
    {
        clearFilterNode(node);
    }
}

void HistogramTreeWidget::addToTreeMap(QObject *object, TreeNode *node)
{
    qDebug() << __PRETTY_FUNCTION__ << object << "->" << node;
    m_treeMap[object] = node;
}

void HistogramTreeWidget::removeFromTreeMap(QObject *object)
{
    qDebug() << __PRETTY_FUNCTION__ << object;
    m_treeMap.remove(object);
}

static const QString fileFilter = QSL("Config Files (*.json);; All Files (*.*)");
static const QString settingsPath = QSL("Files/LastAnalysisConfig");

void HistogramTreeWidget::loadConfig()
{
    QString path = QFileInfo(QSettings().value(settingsPath).toString()).absolutePath();

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getOpenFileName(this, QSL("Load analysis config"), path, fileFilter);

    if (fileName.isEmpty())
        return;

    QJsonDocument doc(gui_read_json_file(fileName));

    if (doc.isNull())
        return;

    auto config = new AnalysisConfig;
    config->read(doc.object()["AnalysisConfig"].toObject());
    m_context->setAnalysisConfig(config);

    QSettings().setValue(settingsPath, fileName);

}

void HistogramTreeWidget::saveConfig()
{
}

void HistogramTreeWidget::saveConfigAs()
{
}
