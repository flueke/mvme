#include "histogram_tree.h"
#include "histogram.h"
#include "hist1d.h"
#include "hist2d.h"
#include "hist2ddialog.h"
#include "mvme_context.h"
#include "mvme_config.h"
#include "treewidget_utils.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTimer>

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

    pb_generateDefaultFilters = new QPushButton("Generate Default Filters");
    connect(pb_generateDefaultFilters, &QPushButton::clicked, this, &HistogramTreeWidget::generateDefaultFilters);

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(pb_generateDefaultFilters);
    buttonLayout->addStretch(1);

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

template<typename T>
TreeNode *makeNode(T *data, int type = QTreeWidgetItem::Type)
{
    auto ret = new TreeNode(type);
    ret->setData(0, DataRole_Pointer, Ptr2Var(data));
    return ret;
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
        m_treeMap[moduleConfig] = moduleNode;
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
        auto moduleConfig = m_context->getDAQConfig()->getModuleConfig(idxPair.first, idxPair.second);
        auto moduleNode = m_treeMap.value(moduleConfig);

        if (moduleNode)
        {
            auto filterNode = makeNode(filterConfig, NodeType_DataFilter);
            filterNode->setText(0, filterConfig->objectName());
            filterNode->setIcon(0, QIcon(":/data_filter.png"));
            moduleNode->addChild(filterNode);
            m_treeMap[filterConfig] = filterNode;

            const auto &filter = filterConfig->getFilter();
            u32 addressCount = 1 << filter.getExtractBits('A');

            for (u32 address = 0; address < addressCount; ++address)
            {
                auto addressNode = makeNode(filterConfig, NodeType_Hist1D);
                addressNode->setText(0, QString::number(address));
                addressNode->setIcon(0, QIcon(":/hist1d.png"));
                addressNode->setData(0, DataRole_FilterAddress, address);
                filterNode->addChild(addressNode);

                auto predicate = [filterConfig, address] (Hist1DConfig *histoConfig)
                {
                    return (histoConfig->getFilterId() == filterConfig->getId()
                            && histoConfig->getFilterAddress() == address);
                };

                auto histoConfig = m_context->getAnalysisConfig()->findChildByPredicate<Hist1DConfig *>(predicate);
                auto histo = m_context->getMappedObject(histoConfig, QSL("ConfigToObject"));

                qDebug() << "found" << histoConfig << histo;

                addressNode->setData(0, DataRole_Pointer, Ptr2Var(histo));
                m_treeMap[histo] = addressNode;
            }
        }
    }

#if 0
    TreeNode *node = nullptr;
    TreeNode *parent = nullptr;

    if(qobject_cast<Hist2D *>(object))
    {
        node = makeNode(object, NodeType_Hist2D);
        parent = m_node2D;
    }

    if (node && parent)
    {
        node->setText(0, object->objectName());
        m_treeMap[object] = node;
        parent->addChild(node);
        m_tree->resizeColumnToContents(0);
    }
#endif
}

void HistogramTreeWidget::onObjectAboutToBeRemoved(QObject *object)
{
    delete m_treeMap.take(object);
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
}

void HistogramTreeWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
    auto obj = Var2Ptr<QObject>(item->data(0, DataRole_Pointer));

    qDebug() << __PRETTY_FUNCTION__ << item << obj;

    if (obj)
        emit objectClicked(obj);
}

void HistogramTreeWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    auto obj = Var2Ptr<QObject>(item->data(0, DataRole_Pointer));
    qDebug() << __PRETTY_FUNCTION__ << item << obj;

    //if (obj)
    //    emit objectDoubleClicked(obj);

    switch (item->type())
    {
        case NodeType_Hist1D:
            emit objectDoubleClicked(obj);
            break;
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

#if 0
    if (node->type() == NodeType_HistoCollection
        || node->type() == NodeType_Histo2D)
    {
        menu.addAction(QSL("Open in new window"), this, [obj, this]() { emit openInNewWindow(obj); });
        menu.addAction(QSL("Clear"), this, &HistogramTreeWidget::clearHistogram);

        menu.addSeparator();
        menu.addAction(QSL("Remove Histogram"), this, &HistogramTreeWidget::removeHistogram);
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

void HistogramTreeWidget::removeHistogram()
{
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);
    auto obj  = Var2Ptr<QObject>(var);

    if (obj)
        m_context->removeObject(obj);
}

void HistogramTreeWidget::add2DHistogram()
{
    Hist2DDialog dialog(m_context);
    int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        auto hist2d = dialog.getHist2D();
        m_context->addObject(hist2d);
        emit openInNewWindow(hist2d);
    }
}

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
        result.push_back(cfg);
    }

    return result;
}

Hist1D *createHistogram(Hist1DConfig *config)
{
    Hist1D *result = new Hist1D(config->getBits());
    result->setProperty("configId", config->getId());
    return result;
}

void HistogramTreeWidget::generateDefaultFilters()
{
    /* for all modules in the daqconfig:
     *   remove all filters and the histograms using those filters
     *   generate the new default filters and their histograms by loading filter templates
     */

    auto daqConfig      = m_context->getDAQConfig();
    auto analysisConfig = m_context->getAnalysisConfig();
    auto eventConfigs   = daqConfig->getEventConfigs();

    for (int eventIndex = 0;
         eventIndex < eventConfigs.size();
         ++eventIndex)
    {
        auto eventConfig = eventConfigs[eventIndex];
        auto moduleConfigs = eventConfig->getModuleConfigs();

        for (int moduleIndex = 0;
             moduleIndex < moduleConfigs.size();
             ++moduleIndex)
        {
            // TODO: remove histograms and their configs created for the filters that are about to be removed

            auto moduleConfig  = moduleConfigs[moduleIndex];
            auto filterConfigs = ::generateDefaultFilters(moduleConfig);

            for (auto filterConfig: filterConfigs)
            {
                auto histoConfigs = generateHistogramConfigs(filterConfig);

                for (auto histoConfig: histoConfigs)
                {
                    analysisConfig->addHist1DConfig(histoConfig);
                    auto histo = createHistogram(histoConfig);
                    histo->setParent(m_context);
                    m_context->addObject(histo);
                    m_context->addObjectMapping(histoConfig, histo, QSL("ConfigToObject"));
                    m_context->addObjectMapping(histo, histoConfig, QSL("ObjectToConfig"));
                }
            }

            analysisConfig->setFilters(eventIndex, moduleIndex, filterConfigs);
        }
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
            auto node = it.value();
            node->setText(1, QString("entries=%1").arg(histo->getEntryCount()));
        }
    }
}
