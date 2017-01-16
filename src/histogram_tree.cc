#include "histogram_tree.h"
#include "histogram.h"
#include "hist1d.h"
#include "hist2d.h"
#include "hist2ddialog.h"
#include "mvme_context.h"
#include "mvme_config.h"
#include "treewidget_utils.h"
#include "config_ui.h"
#include "mvme_event_processor.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <memory>

//
// Utility functions for filter and histogram creation.
//

static DataFilterConfigList generateDefaultFilters(ModuleConfig *moduleConfig)
{
    DataFilterConfigList result;

    const auto filterDefinitions = defaultDataFilters.value(moduleConfig->type);

    for (const auto &def: filterDefinitions)
    {
        auto cfg = new DataFilterConfig(QByteArray(def.filter));
        cfg->setObjectName(def.name);
        cfg->setAxisTitle(def.title);
        result.push_back(cfg);
    }

    return result;
}

static DualWordDataFilterConfigList generateDefaultDualWordFilters(ModuleConfig *moduleConfig)
{
    DualWordDataFilterConfigList result;

    const auto filterDefinitions = defaultDualWordFilters.value(moduleConfig->type);

    for (const auto &def: filterDefinitions)
    {
        auto cfg = new DualWordDataFilterConfig(
            DualWordDataFilter(
                DataFilter(def.lowFilter, def.lowIndex),
                DataFilter(def.highFilter, def.highIndex)));

        cfg->setObjectName(def.name);
        cfg->setAxisTitle(def.title);
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

        updateHistogramConfigFromFilterConfig(cfg, filterConfig);

        result.push_back(cfg);
    }

    return result;
}

static Hist1DConfig *generateDifferenceHistogramConfig(DualWordDataFilterConfig *filterConfig)
{
    static const u32 dualWordFilterHistoBits = 16;

    auto result = new Hist1DConfig;
    result->setObjectName(QString("%1 Differences").arg(filterConfig->objectName()));
    result->setFilterId(filterConfig->getId());
    result->setBits(dualWordFilterHistoBits);
    updateHistogramConfigFromFilterConfig(result, filterConfig);

    return result;
}

static Hist1D *createAndAddHist1D(MVMEContext *context, Hist1DConfig *histoConfig)
{
    context->getAnalysisConfig()->addHist1DConfig(histoConfig);
    auto histo = createHistogram(histoConfig, context);
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
    NodeType_DualWordDataFilter,
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
    auto makeToolButton = [](const QString &icon, const QString &text)
    {
        auto result = new QToolButton;
        result->setIcon(QIcon(icon));
        result->setText(text);
        result->setStatusTip(text);
        result->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        //result->setToolButtonStyle(Qt::ToolButtonIconOnly);
        auto font = result->font();
        font.setPointSize(7);
        result->setFont(font);
        return result;
    };

    pb_new = makeToolButton(QSL(":/document-new.png"), QSL("New"));
    pb_load = makeToolButton(QSL(":/document-open.png"), QSL("Open"));
    pb_save = makeToolButton(QSL(":/document-save.png"), QSL("Save"));
    pb_saveAs = makeToolButton(QSL(":/document-save-as.png"), QSL("Save As"));

    connect(pb_new, &QAbstractButton::clicked, this, &HistogramTreeWidget::newConfig);
    connect(pb_load, &QAbstractButton::clicked, this, &HistogramTreeWidget::loadConfig);
    connect(pb_save, &QAbstractButton::clicked, this, &HistogramTreeWidget::saveConfig);
    connect(pb_saveAs, &QAbstractButton::clicked, this, &HistogramTreeWidget::saveConfigAs);

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(pb_new);
    buttonLayout->addWidget(pb_load);
    buttonLayout->addWidget(pb_save);
    buttonLayout->addWidget(pb_saveAs);
    buttonLayout->addStretch(1);

    // filename label
    le_fileName = new QLineEdit;
    le_fileName->setReadOnly(true);
    auto pal = le_fileName->palette();
    pal.setBrush(QPalette::Base, QColor(239, 235, 231));
    le_fileName->setPalette(pal);

    // widget layout
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttonLayout);
    layout->addWidget(le_fileName);
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
    connect(m_context, &MVMEContext::analysisConfigFileNameChanged, this, [this](const QString &) {
        updateConfigLabel();
    });

    onAnyConfigChanged();

    auto timer = new QTimer(this);
    timer->setInterval(1000);
    timer->start();
    connect(timer, &QTimer::timeout, this, &HistogramTreeWidget::updateHistogramCountDisplay);
}

void HistogramTreeWidget::onObjectAdded(QObject *object)
{
    qDebug() << __PRETTY_FUNCTION__ << object;

    if (m_treeMap.contains(object))
        return;

    if (auto eventConfig = qobject_cast<EventConfig *>(object))
    {
        connect(eventConfig, &EventConfig::moduleAdded, this, &HistogramTreeWidget::onObjectAdded);
        connect(eventConfig, &EventConfig::moduleAboutToBeRemoved, this, &HistogramTreeWidget::onObjectAboutToBeRemoved);

        for (auto moduleConfig: eventConfig->getModuleConfigs())
            onObjectAdded(moduleConfig);
        m_tree->resizeColumnToContents(0);
    }
    else if (auto moduleConfig = qobject_cast<ModuleConfig *>(object))
    {
        auto moduleNode = makeNode(moduleConfig, NodeType_Module);
        moduleNode->setText(0, moduleConfig->objectName());
        moduleNode->setIcon(0, QIcon(":/vme_module.png"));
        addToTreeMap(moduleConfig, moduleNode);
        m_node1D->addChild(moduleNode);

        auto idxPair = m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig);

        moduleNode->setText(1, QString("event=%1, module=%2").arg(idxPair.first).arg(idxPair.second));

        for (auto filterConfig: m_context->getAnalysisConfig()->getFilters(idxPair.first, idxPair.second))
            onObjectAdded(filterConfig);

        m_tree->resizeColumnToContents(0);

        connect(moduleConfig, &QObject::objectNameChanged, this, [this, moduleConfig](const QString &name) {
            onObjectNameChanged(moduleConfig, name);
        });

        moduleNode->setExpanded(true);
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

        if (!moduleConfig)
        {
            return;
        }

        auto moduleNode = m_treeMap.value(moduleConfig);

        // TODO (maybe): add nodes for filters that don't have a corresponding module in the daq config
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
            m_tree->resizeColumnToContents(0);

            connect(filterConfig, &QObject::objectNameChanged, this, [this, filterConfig](const QString &name) {
                onObjectNameChanged(filterConfig, name);
            });
        }
        else
        {
            qDebug() << __PRETTY_FUNCTION__ << "!!! no module node found for filter config" << filterConfig << "and module config" << moduleConfig;
        }
    }
    else if (auto filterConfig = qobject_cast<DualWordDataFilterConfig *>(object))
    {
        auto idxPair = m_context->getAnalysisConfig()->getEventAndModuleIndices(filterConfig);
        if (idxPair.first < 0)
        {
            qDebug() << __PRETTY_FUNCTION__ << "!!! invalid analysisconfig indices for DualWordDataFilterConfig" << filterConfig;
            return;
        }
        auto moduleConfig = m_context->getDAQConfig()->getModuleConfig(idxPair.first, idxPair.second);

        if (!moduleConfig)
        {
            return;
        }

        auto moduleNode = m_treeMap.value(moduleConfig);

        if (moduleNode)
        {
            auto filterNode = makeNode(filterConfig, NodeType_DualWordDataFilter);
            filterNode->setText(0, filterConfig->objectName());
            filterNode->setIcon(0, QIcon(":/data_filter.png"));
            moduleNode->addChild(filterNode);
            addToTreeMap(filterConfig, filterNode);

            m_tree->resizeColumnToContents(0);

            connect(filterConfig, &QObject::objectNameChanged, this, [this, filterConfig](const QString &name) {
                onObjectNameChanged(filterConfig, name);
            });
        }
    }
    else if (auto histo = qobject_cast<Hist2D *>(object))
    {
        auto histoConfig = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(histo, QSL("ObjectToConfig")));

        if (histoConfig)
        {
            auto node = makeNode(object, NodeType_Hist2D);
            node->setText(0, histoConfig->objectName());
            m_node2D->addChild(node);
            addToTreeMap(object, node);
            m_tree->resizeColumnToContents(0);
        }
    }
}

void HistogramTreeWidget::onObjectAboutToBeRemoved(QObject *object)
{
    qDebug() << __PRETTY_FUNCTION__ << object;
    if (auto node = m_treeMap.value(object, nullptr))
        removeNode(node);
}

void HistogramTreeWidget::removeNode(QTreeWidgetItem *node)
{
    auto obj = Var2Ptr<QObject>(node->data(0, DataRole_Pointer));
    removeFromTreeMap(obj);

    for (auto childNode: node->takeChildren())
    {
        removeNode(childNode);
    }

    delete node;
}

void HistogramTreeWidget::onAnyConfigChanged()
{
    qDebug() << __PRETTY_FUNCTION__ << "begin";

    qDeleteAll(m_node1D->takeChildren());
    qDeleteAll(m_node2D->takeChildren());
    m_treeMap.clear();

    bool daqChanged = m_daqConfig != m_context->getDAQConfig();
    bool analysisChanged = m_analysisConfig != m_context->getAnalysisConfig();

    m_daqConfig = m_context->getDAQConfig();
    m_analysisConfig = m_context->getAnalysisConfig();

    if (m_daqConfig)
    {
        for (auto eventConfig: m_daqConfig->getEventConfigs())
            onObjectAdded(eventConfig);

        connect(m_daqConfig, &DAQConfig::eventAdded, this, &HistogramTreeWidget::onObjectAdded);
    }

    if (m_analysisConfig)
    {
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
        }

        {
            auto filters = m_analysisConfig->getDualWordFilters();

            for (int eventIndex: filters.keys())
            {
                for (int moduleIndex: filters[eventIndex].keys())
                {
                    for (auto filter: filters[eventIndex][moduleIndex])
                        onObjectAdded(filter);
                }
            }
        }

        for (auto hist2d: m_context->getObjects<Hist2D *>())
        {
            onObjectAdded(hist2d);
        }

        if (analysisChanged)
        {
            connect(m_analysisConfig, &ConfigObject::modifiedChanged, this, [this](bool) {
                updateConfigLabel();
            });
        }

        updateConfigLabel();
    }
    qDebug() << __PRETTY_FUNCTION__ << "end";
}

void HistogramTreeWidget::onObjectNameChanged(QObject *object, const QString &name)
{
    if (auto node = m_treeMap.value(object, nullptr))
    {
        node->setText(0, name);
    }
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
        case NodeType_Hist2D:
            {
                emit objectDoubleClicked(obj);
            } break;
        case NodeType_DataFilter:
            {
                openHistoListWidget();
            } break;

        case NodeType_DualWordDataFilter:
            {
                auto filterConfig = qobject_cast<DualWordDataFilterConfig *>(obj);
                if (filterConfig)
                {
                    auto histoConfig = m_context->getAnalysisConfig()->findChildByPredicate<Hist1DConfig *>(
                        [filterConfig](Hist1DConfig *histoConfig) {
                            return histoConfig->getFilterId() == filterConfig->getId();
                        });
                    if (histoConfig)
                    {
                        auto histo = m_context->getObjectForConfig(histoConfig);
                        emit objectDoubleClicked(histo);
                    }
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
    auto parent = node ? node->parent() : nullptr;
    auto obj = node ? Var2Ptr<ConfigObject>(node->data(0, DataRole_Pointer)) : nullptr;
    auto isIdle = m_context->getDAQState() == DAQState::Idle;

    QMenu menu;

    if (node == m_node1D)
    {
        menu.addAction(QSL("Clear Histograms"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(&HistogramTreeWidget::clearHistograms));
    }

    if (node && node->type() == NodeType_Module)
    {
        menu.addAction(QSL("Clear Histograms"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(&HistogramTreeWidget::clearHistograms));

        menu.addAction(QSL("Add filter"), this, &HistogramTreeWidget::addDataFilter)->setEnabled(isIdle);
        menu.addAction(QSL("Add dual word filter"), this, &HistogramTreeWidget::addDualWordDataFilter)->setEnabled(isIdle);
        menu.addAction(QSL("Generate default filters"), this, &HistogramTreeWidget::generateDefaultFilters)->setEnabled(isIdle);

        if (!m_context->getEventProcessor()->getDiagnostics())
            menu.addAction(QSL("Show Diagnostics"), this, &HistogramTreeWidget::handleShowDiagnostics);

    }

    if (node && node->type() == NodeType_DataFilter)
    {
        menu.addAction(QSL("Open histogram list"), this, &HistogramTreeWidget::openHistoListWidget);

        menu.addSeparator();
        menu.addAction(QSL("Clear Histograms"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(&HistogramTreeWidget::clearHistograms));

        menu.addSeparator();
        menu.addAction(QSL("Edit filter"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(
                       &HistogramTreeWidget::editDataFilter))->setEnabled(isIdle);

        menu.addAction(QSL("Remove filter"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(
                       &HistogramTreeWidget::removeDataFilter))->setEnabled(isIdle);
    }

    if (node && node->type() == NodeType_DualWordDataFilter)
    {
        menu.addAction(QSL("Clear Histogram"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(&HistogramTreeWidget::clearHistograms));

        menu.addSeparator();

        menu.addAction(QSL("Edit filter"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(
                       &HistogramTreeWidget::editDualWordDataFilter))->setEnabled(isIdle);

        menu.addAction(QSL("Remove filter"), this,
                       static_cast<void (HistogramTreeWidget::*) ()>(
                       &HistogramTreeWidget::removeDualWordDataFilter))->setEnabled(isIdle);
    }

    if (node && node->type() == NodeType_Hist1D)
    {
        menu.addAction(QSL("Open in new window"), this, [obj, this]() { emit openInNewWindow(obj); });
        menu.addAction(QSL("Clear"), this, &HistogramTreeWidget::clearHistogram);
    }

    if (node && node->type() == NodeType_Hist2D)
    {
        menu.addAction(QSL("Open in new window"), this, [obj, this]() { emit openInNewWindow(obj); });
        menu.addAction(QSL("Clear"), this, &HistogramTreeWidget::clearHistogram);
        menu.addSeparator();
        menu.addAction(QSL("Edit Histogram"), this, &HistogramTreeWidget::edit2DHistogram);
        menu.addAction(QSL("Remove Histogram"), this, &HistogramTreeWidget::removeHistogram)->setEnabled(isIdle);
    }

    if (node == m_node2D && m_context->getConfig()->getAllModuleConfigs().size())
    {
        menu.addAction(QSL("Add 2D Histogram"), this, &HistogramTreeWidget::add2DHistogram)->setEnabled(isIdle);
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
    Hist2DDialog dialog(m_context, this);
    int result = dialog.exec();

    if (result == QDialog::Accepted)
    {
        auto histoAndConfig = dialog.getHistoAndConfig();
        auto histo = histoAndConfig.first;
        auto histoConfig = histoAndConfig.second;
        m_context->registerObjectAndConfig(histo, histoConfig);
        m_context->getAnalysisConfig()->addHist2DConfig(histoConfig);
        emit openInNewWindow(histo);
    }
}

void HistogramTreeWidget::edit2DHistogram()
{
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);
    if (auto histo = Var2QObject<Hist2D>(var))
    {
        Hist2DDialog dialog(m_context, histo);
        int result = dialog.exec();

        if (result == QDialog::Accepted)
        {
            dialog.getHistoAndConfig(); // updates both histo and config
            histo->clear();
        }
    }
}

void HistogramTreeWidget::removeHistogram()
{
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);

    if (auto histo = Var2QObject<Hist2D>(var))
    {
        auto histoConfig = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(histo, QSL("ObjectToConfig")));

        m_context->unregisterObjectAndConfig(histo, histoConfig);
        m_context->removeObject(histo);
        m_analysisConfig->removeHist2DConfig(histoConfig);
    }
}

void HistogramTreeWidget::updateHistogramCountDisplay()
{
    const auto dualWordValues = m_context->getEventProcessor()->getDualWordFilterValues();
    const auto dualWordDiffs  = m_context->getEventProcessor()->getDualWordFilterDiffs();

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
        else if (auto filterConfig = qobject_cast<DualWordDataFilterConfig *>(it.key()))
        {
            auto values = dualWordValues.value(filterConfig);
            auto diffs  = dualWordDiffs.value(filterConfig);

            // XXX: this only displays the first value that matched
            if (!values.isEmpty())
            {
                auto node = it.value();
                if (diffs.isEmpty())
                    node->setText(1, QString("val=%1").arg(values[0]));
                else
                    node->setText(1, QString("val=%1, diff=%2").arg(values[0]).arg(diffs[0]));
            }
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

    for (int childIndex = 0;
         childIndex < node->childCount();
        )
    {
        auto filterNode = node->child(childIndex);
        if (filterNode->type() == NodeType_DataFilter)
        {
            removeDataFilter(filterNode);
        }
        else if (filterNode->type() == NodeType_DualWordDataFilter)
        {
            removeDualWordDataFilter(filterNode);
        }
        else
        {
            ++childIndex;
        }
    }

    {
        auto filterConfigs = ::generateDefaultFilters(moduleConfig);

        qDebug() << __PRETTY_FUNCTION__ << "generated filters:" << filterConfigs;

        for (auto filterConfig: filterConfigs)
        {
            for (auto histoConfig: generateHistogramConfigs(filterConfig))
                createAndAddHist1D(m_context, histoConfig);
        }

        m_context->getAnalysisConfig()->setFilters(indices.first, indices.second, filterConfigs);
    }

    {
        auto filterConfigs = ::generateDefaultDualWordFilters(moduleConfig);

        for (auto filterConfig: filterConfigs)
        {
            /* Create and add the single histogram holding the difference
             * values generated using the filter. */
            auto histoConfig = generateDifferenceHistogramConfig(filterConfig);
            createAndAddHist1D(m_context, histoConfig);
        }

        m_context->getAnalysisConfig()->setDualWordFilters(indices.first, indices.second, filterConfigs);
    }

    node->setExpanded(true);
}

void HistogramTreeWidget::addDataFilter()
{
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);
    auto moduleConfig = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    auto defaultFilter = defaultDataFilters.value(moduleConfig->type).value(0).filter;
    std::unique_ptr<DataFilterConfig> filterConfig(new DataFilterConfig(DataFilter(defaultFilter)));

    DataFilterDialog dialog(filterConfig.get(), defaultFilter);

    if (dialog.exec() == QDialog::Accepted)
    {
        for (auto histoConfig: generateHistogramConfigs(filterConfig.get()))
            createAndAddHist1D(m_context, histoConfig);

        auto indices = m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig);
        if (indices.first < 0)
        {
            qDebug() << __PRETTY_FUNCTION__ << "invalid daqconfig indices for moduleConfig" << moduleConfig;
            return;
        }
        m_context->getAnalysisConfig()->addFilter(indices.first, indices.second, filterConfig.release());
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

void HistogramTreeWidget::editDataFilter()
{
    auto node = m_tree->currentItem();
    editDataFilter(node);
}

void HistogramTreeWidget::editDataFilter(QTreeWidgetItem *node)
{
    auto moduleNode   = node->parent();
    auto moduleConfig = Var2Ptr<ModuleConfig>(moduleNode->data(0, DataRole_Pointer));
    auto defaultFilter = QString(defaultDataFilters.value(moduleConfig->type).value(0).filter);
    auto filterConfig = Var2Ptr<DataFilterConfig>(node->data(0, DataRole_Pointer));
    auto preEditFilter = filterConfig->getFilter();

    DataFilterDialog dialog(filterConfig, defaultFilter);

    if (dialog.exec() == QDialog::Accepted)
    {
        qDebug() << "<<<<< begin edited filter";

        if (preEditFilter != filterConfig->getFilter())
        {
            /* The filter string was modified, so the number of histograms and
             * the resolution might have changed. In this case just remove the
             * existing histograms and then generate new ones from the filter.
             * TODO: what about 2d histos referencing this filter?
             */
            auto histoNodes = node->takeChildren();

            for (auto histoNode: histoNodes)
                removeHist1D(histoNode);

            // generate new histograms from the filter
            for (auto histoConfig: generateHistogramConfigs(filterConfig))
                createAndAddHist1D(m_context, histoConfig);

            // generate the histogram nodes
            auto histoNodePairs = generateHistogramNodes(m_context, filterConfig);

            for (auto pair: histoNodePairs)
            {
                node->addChild(pair.first);
                addToTreeMap(pair.second, pair.first);
            }
        }
        else
        {
            /* The filter string is unchanged. Update histograms referencing
             * this filterConfig. */
            m_context->getAnalysisConfig()->updateHistogramsForFilter(filterConfig);
        }

        qDebug() << "<<<<< end edited filter";
    }
}

void HistogramTreeWidget::addDualWordDataFilter()
{
    auto node = m_tree->currentItem();
    auto var  = node->data(0, DataRole_Pointer);
    auto moduleConfig = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    std::unique_ptr<DualWordDataFilterConfig> filterConfig(new DualWordDataFilterConfig);

    if (!defaultDualWordFilters.value(moduleConfig->type).isEmpty())
    {
        auto defaultFilterDef = defaultDualWordFilters.value(moduleConfig->type).value(0);
        if (defaultFilterDef.lowFilter && defaultFilterDef.highFilter)
        {
            DualWordDataFilter filter(
                DataFilter(defaultFilterDef.lowFilter, defaultFilterDef.lowIndex),
                DataFilter(defaultFilterDef.highFilter, defaultFilterDef.highIndex));

            filterConfig->setFilter(filter);
        }
    }

    DualWordDataFilterDialog dialog(filterConfig.get());

    if (dialog.exec() == QDialog::Accepted)
    {

        auto indices = m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig);
        if (indices.first < 0)
        {
            qDebug() << __PRETTY_FUNCTION__ << "invalid daqconfig indices for moduleConfig" << moduleConfig;
            return;
        }

        auto histoConfig = generateDifferenceHistogramConfig(filterConfig.get());
        createAndAddHist1D(m_context, histoConfig);

        m_context->getAnalysisConfig()->addDualWordFilter(indices.first, indices.second, filterConfig.release());
    }
}

void HistogramTreeWidget::removeDualWordDataFilter()
{
    auto node = m_tree->currentItem();
    removeDualWordDataFilter(node);
}

void HistogramTreeWidget::removeDualWordDataFilter(QTreeWidgetItem *node)
{
    Q_ASSERT(node->type() == NodeType_DualWordDataFilter);
    auto filterConfig = Var2Ptr<DualWordDataFilterConfig>(node->data(0, DataRole_Pointer));

    Q_ASSERT(filterConfig);
    Q_ASSERT(m_treeMap[filterConfig] == node);

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
    m_context->getAnalysisConfig()->removeDualWordFilter(indices.first, indices.second, filterConfig);

    // remove the difference histogram
    auto histoConfig = m_context->getAnalysisConfig()->findChildByPredicate<Hist1DConfig *>(
        [filterConfig](Hist1DConfig *histoConfig) {
            return histoConfig->getFilterId() == filterConfig->getId();
        });

    if (histoConfig)
    {
        auto histo = m_context->getObjectForConfig(histoConfig);
        removeFromTreeMap(histo);
        m_context->unregisterObjectAndConfig(histo, histoConfig);
        m_context->removeObject(histo);
        m_context->getAnalysisConfig()->removeHist1DConfig(histoConfig);
    }
}

void HistogramTreeWidget::editDualWordDataFilter()
{
    auto node = m_tree->currentItem();
    editDualWordDataFilter(node);
}

void HistogramTreeWidget::editDualWordDataFilter(QTreeWidgetItem *node)
{
    auto moduleNode   = node->parent();
    auto moduleConfig = Var2Ptr<ModuleConfig>(moduleNode->data(0, DataRole_Pointer));
    auto filterConfig = Var2Ptr<DualWordDataFilterConfig>(node->data(0, DataRole_Pointer));
    auto preEditFilter = filterConfig->getFilter();

    DualWordDataFilterDialog dialog(filterConfig);

    if (dialog.exec() == QDialog::Accepted)
    {
        auto histoConfig = m_context->getAnalysisConfig()->findChildByPredicate<Hist1DConfig *>(
            [filterConfig](Hist1DConfig *histoConfig) {
                return histoConfig->getFilterId() == filterConfig->getId();
            });

        if (histoConfig)
        {
            updateHistogramConfigFromFilterConfig(histoConfig, filterConfig);
        }
    }
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

static void filterNodeClearHistos(QTreeWidgetItem *filterNode)
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
}

static void dualWordFilterNodeClearHistos(MVMEContext *context, QTreeWidgetItem *filterNode)
{
    auto obj = Var2Ptr<QObject>(filterNode->data(0, DataRole_Pointer));
    auto filterConfig = qobject_cast<DualWordDataFilterConfig *>(obj);
    if (filterConfig)
    {
        auto histoConfig = context->getAnalysisConfig()->findChildByPredicate<Hist1DConfig *>(
            [filterConfig](Hist1DConfig *histoConfig) {
                return histoConfig->getFilterId() == filterConfig->getId();
            });

        if (histoConfig)
        {
            auto histo = qobject_cast<Hist1D *>(context->getObjectForConfig(histoConfig));
            if (histo)
                histo->clear();
        }
    }
}

static void moduleNodeClearHistos(MVMEContext *context, QTreeWidgetItem *moduleNode)
{
    for (int i = 0; i < moduleNode->childCount(); ++i)
    {
        auto filterNode = moduleNode->child(i);
        if (filterNode->type() == NodeType_DataFilter)
            filterNodeClearHistos(filterNode);
        else if (filterNode->type() == NodeType_DualWordDataFilter)
            dualWordFilterNodeClearHistos(context, filterNode);
    }
}

void HistogramTreeWidget::clearHistograms()
{
    auto node = m_tree->currentItem();
    clearHistograms(node);
}

void HistogramTreeWidget::clearHistograms(QTreeWidgetItem *node)
{
    if (node->type() == NodeType_Module)
    {
        moduleNodeClearHistos(m_context, node);
    }
    else if (node->type() == NodeType_DataFilter)
    {
        filterNodeClearHistos(node);
    }
    else if (node->type() == NodeType_DualWordDataFilter)
    {
        dualWordFilterNodeClearHistos(m_context, node);
    }
    else if (node == m_node1D)
    {
        for (int i = 0; i < node->childCount(); ++i)
        {
            clearHistograms(node->child(i));
        }
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

void HistogramTreeWidget::newConfig()
{
    auto analysisConfig = m_context->getAnalysisConfig();
    if (analysisConfig->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis config?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!saveConfig())
                return;
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    m_context->setAnalysisConfig(new AnalysisConfig);
    m_context->setAnalysisConfigFileName(QString());
}

void HistogramTreeWidget::loadConfig()
{
    if (m_context->getAnalysisConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis config?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!saveConfig())
                return;
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    auto path = m_context->getWorkspaceDirectory();

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getOpenFileName(this, QSL("Load analysis config"), path, fileFilter);

    if (fileName.isEmpty())
        return;

    m_context->loadAnalysisConfig(fileName);
}

bool HistogramTreeWidget::saveConfig()
{
    auto analysisConfig = m_context->getAnalysisConfig();
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
    {
        auto result = saveAnalysisConfigAs(analysisConfig, m_context->getWorkspaceDirectory());
        if (result.first)
        {
            analysisConfig->setModified(false);
            for (auto obj: analysisConfig->findChildren<ConfigObject *>())
                obj->setModified(false);
            m_context->setAnalysisConfigFileName(result.second);
            return true;
        }
    }
    else if (saveAnalysisConfig(analysisConfig, fileName, m_context->getWorkspaceDirectory()).first)
    {
        analysisConfig->setModified(false);
        for (auto obj: analysisConfig->findChildren<ConfigObject *>())
            obj->setModified(false);
        return true;
    }
    return false;
}

bool HistogramTreeWidget::saveConfigAs()
{
    auto analysisConfig = m_context->getAnalysisConfig();
    auto result = saveAnalysisConfigAs(analysisConfig, m_context->getWorkspaceDirectory());

    if (result.first)
    {
        analysisConfig->setModified(false);
        for (auto obj: analysisConfig->findChildren<ConfigObject *>())
            obj->setModified(false);
        m_context->setAnalysisConfigFileName(result.second);
    }

    return result.first;
}

void HistogramTreeWidget::updateConfigLabel()
{
    QString fileName = m_context->getAnalysisConfigFileName();

    if (fileName.isEmpty())
        fileName = QSL("<not saved>");

    if (m_context->getAnalysisConfig()->isModified())
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

void HistogramTreeWidget::handleShowDiagnostics()
{
    auto node = m_tree->currentItem();
    auto module = Var2Ptr<ModuleConfig>(node->data(0, DataRole_Pointer));
    emit showDiagnostics(module);
}

void HistogramTreeWidget::openHistoListWidget()
{
    auto node = m_tree->currentItem();

    if (node->type() != NodeType_DataFilter)
        return;

    auto filter = Var2Ptr<DataFilterConfig>(node->data(0, DataRole_Pointer));

    if (!filter)
        return;

    QList<Hist1D *> histograms;

    for (int i=0; i < node->childCount(); ++i)
    {
        auto histoNode = node->child(i);
        if (histoNode->type() == NodeType_Hist1D)
        {
            auto histo = Var2Ptr<Hist1D>(histoNode->data(0, DataRole_Pointer));
            if (histo)
                histograms.push_back(histo);
        }
    }

    if (!histograms.isEmpty())
    {
        auto widget = new Hist1DListWidget(m_context, histograms, this);
        emit addWidgetWindow(widget);
    }
}
