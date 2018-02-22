#include "rate_monitor_widget.h"

#include <QBoxLayout>
#include <QCollator>
#include <QGroupBox>
#include <QHeaderView>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVector>

#include "rate_monitoring.h"
#include "util/tree.h"
#include "util/bihash.h"

class RateTreeWidgetNode: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;

        // Custom sorting for numeric columns
        virtual bool operator<(const QTreeWidgetItem &other) const override
        {
            int sortColumn = treeWidget() ? treeWidget()->sortColumn() : 0;

            if (sortColumn == 0)
            {
                QCollator collator;
                collator.setNumericMode(true);

                auto s1 = this->data(sortColumn, Qt::DisplayRole).toString();
                auto s2 = other.data(sortColumn, Qt::DisplayRole).toString();
                auto result = collator.compare(s1, s2) < 0;

                return result;
            }

            return QTreeWidgetItem::operator<(other);
        }
};

enum TreeWidgetNodeType
{
    NodeType_Group,
    NodeType_SystemRate,
};

template<typename T>
T *getPointer(QTreeWidgetItem *node, s32 dataRole = Qt::UserRole)
{
    return node ? reinterpret_cast<T *>(node->data(0, dataRole).value<void *>()) : nullptr;
}

struct RateMonitorWidgetPrivate
{
    QTreeWidget *m_rateTreeWidget;
    QGroupBox *m_propertyBox;

    QTableWidget *m_rateTable;
    RateMonitorPlotWidget *m_plotWidget;

    MVMEContext *m_context;

    QVector<RateSampler *> m_entries;

    DAQStatsSampler m_daqStatsSampler;
    StreamProcessorSampler m_streamProcSampler;
    SIS3153Sampler m_sisReadoutSampler;

    RateMonitorNode m_rootNode;

    // sampler tree and sampling
    void createRateMonitorTree(RateMonitorNode &root);
    void doInternalSampling();
    void dev_test_setup_thingy(); // XXX

    // tree widget
    void repopulateTreeWidget();
    void onTreeWidgetCurrentItemChanged(QTreeWidgetItem *cur, QTreeWidgetItem *prev);
    void onTreeWidgetItemChanged(QTreeWidgetItem *item, int column);

    // rate table stuff. TODO: move this to a RateTable abstraction at some point
    void addRateEntryToTable(RateSampler *entry, const QString &name);
    void removeRateEntryFromTable(RateSampler *entry);
    void updateRateTable();
    BiHash<RateSampler *, QTableWidgetItem *> m_rateTableHash; // FIXME: one item for each cell, not one item for a row!


};

void RateMonitorWidgetPrivate::createRateMonitorTree(RateMonitorNode &root)
{
    *root.putBranch("streamProc")         = m_streamProcSampler.createTree();
    *root.putBranch("readout")            = m_daqStatsSampler.createTree();
    *root.putBranch("readout.sis3153")    = m_sisReadoutSampler.createTree();
    root.assertParentChildIntegrity();
}

void RateMonitorWidgetPrivate::doInternalSampling()
{
    m_daqStatsSampler.sample(m_context->getDAQStats());
    m_streamProcSampler.sample(m_context->getMVMEStreamWorker()->getStreamProcessor()->getCounters());

    if (auto sis = qobject_cast<SIS3153ReadoutWorker *>(m_context->getReadoutWorker()))
    {
        m_sisReadoutSampler.sample(sis->getCounters());
    }
}

static void populate(QTreeWidgetItem *widgetRoot, RateMonitorNode &rateRoot)
{
    for (auto it = rateRoot.begin(); it != rateRoot.end(); it++)
    {
        auto key = it.key();
        auto &rateNode = it.value();

        auto item = new RateTreeWidgetNode(static_cast<int>(rateNode.data().type));
        item->setText(0, key);
        item->setText(1, rateNode.data().description);
        // Store a pointer to the RateMonitorNode in the tree item
        item->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<void *>(&rateNode)));

        switch (rateNode.data().type)
        {
            case RateMonitorEntry::Type::Group:
                break;

            case RateMonitorEntry::Type::SystemRate:
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, rateNode.data().sampler->rateHistory ? Qt::Checked : Qt::Unchecked);
                break;
        }

        widgetRoot->addChild(item);
        populate(item, rateNode); // recurse
    }
}

void RateMonitorWidgetPrivate::repopulateTreeWidget()
{
    auto treeWidget = m_rateTreeWidget;

    treeWidget->clear();

    auto widgetRoot = treeWidget->invisibleRootItem();
    populate(widgetRoot, m_rootNode);
    treeWidget->sortItems(0, Qt::AscendingOrder);
}

void RateMonitorWidgetPrivate::onTreeWidgetCurrentItemChanged(QTreeWidgetItem *cur,
                                                              QTreeWidgetItem *prev)
{
    qDebug() << __PRETTY_FUNCTION__ << "cur =" << cur
        << ", prev =" << prev;
}

void RateMonitorWidgetPrivate::onTreeWidgetItemChanged(QTreeWidgetItem *item, int column)
{
    qDebug() << __PRETTY_FUNCTION__ << "item =" << item << ", column =" << column;

    switch (static_cast<RateMonitorEntry::Type>(item->type()))
    {
        case RateMonitorEntry::Type::Group:
            assert(false);
            break;

        case RateMonitorEntry::Type::SystemRate:
            auto node = getPointer<RateMonitorNode>(item);
            assert(node);

            auto &rme = node->data();
            if (rme.sampler)
            {
                if (item->checkState(0) == Qt::Checked)
                {
                    if (!rme.sampler->rateHistory)
                    {
                        qDebug() << "creating new history buffer for" << node->path();
                        rme.sampler->rateHistory = std::make_shared<RateHistoryBuffer>(3600);
                        m_plotWidget->addRate(rme.sampler->rateHistory, node->path());
                    }
                }
            }
            break;
    }
}

void RateMonitorWidgetPrivate::dev_test_setup_thingy() // XXX
{
#if 0
    static const size_t RateHistorySampleCapacity = 60 * 60;

    m_streamProcSampler.bytesProcessed.rateHistory = std::make_shared<RateHistoryBuffer>(RateHistorySampleCapacity);
    m_streamProcSampler.bytesProcessed.scale = 1.0 / Kilobytes(1.0);
    m_plotWidget->addRate(m_streamProcSampler.bytesProcessed.rateHistory, "streamProc.bytes", Qt::black);

    m_streamProcSampler.buffersProcessed.rateHistory = std::make_shared<RateHistoryBuffer>(RateHistorySampleCapacity);
    m_plotWidget->addRate(m_streamProcSampler.buffersProcessed.rateHistory, "streamProc.buffers", Qt::red);
#endif
}

void RateMonitorWidgetPrivate::addRateEntryToTable(RateSampler *entry, const QString &name)
{
    //assert(!m_rateTableHash.map.contains(entry));

    //auto item = new QTableWidgetItem(name);
}

void RateMonitorWidgetPrivate::removeRateEntryFromTable(RateSampler *entry)
{
    assert(!"not implemented");
}

void RateMonitorWidgetPrivate::updateRateTable()
{
    //assert(!"not implemented");
}

RateMonitorWidget::RateMonitorWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorWidgetPrivate>())
{
    setWindowTitle(QSL("Rate Monitor"));

    m_d->m_rateTreeWidget = new QTreeWidget;
    m_d->m_rateTable = new QTableWidget;
    m_d->m_plotWidget = new RateMonitorPlotWidget;
    m_d->m_plotWidget->setXAxisReversed(true);
    m_d->m_propertyBox = new QGroupBox("Properties");
    m_d->m_context = context;

    // rate tree widget setup
    {
        const QStringList columnTitles = { "Object", "Description" };
        auto tree = m_d->m_rateTreeWidget;
        tree->header()->setVisible(true);
        tree->setColumnCount(columnTitles.size());
        tree->setHeaderLabels(columnTitles);
        tree->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        connect(tree, &QTreeWidget::currentItemChanged, this, [this] (QTreeWidgetItem *cur, QTreeWidgetItem *prev) {
            m_d->onTreeWidgetCurrentItemChanged(cur, prev);
        });

        connect(tree, &QTreeWidget::itemChanged, this, [this] (QTreeWidgetItem *item, int column) {
            m_d->onTreeWidgetItemChanged(item, column);
        });
    }

    // rate table setup
    {
        auto table = m_d->m_rateTable;
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({"Name", "Rate", "Value"});
    }

    auto leftVSplitter = new QSplitter(Qt::Vertical);
    leftVSplitter->addWidget(m_d->m_rateTreeWidget);
    leftVSplitter->addWidget(m_d->m_propertyBox);
    leftVSplitter->setStretchFactor(0, 10);
    leftVSplitter->setStretchFactor(1, 1);

    auto rightVSplitter = new QSplitter(Qt::Vertical);
    rightVSplitter->addWidget(m_d->m_rateTable);
    rightVSplitter->addWidget(m_d->m_plotWidget);
    rightVSplitter->setStretchFactor(1, 1);

    auto widgetHSplitter = new QSplitter;
    widgetHSplitter->addWidget(leftVSplitter);
    widgetHSplitter->addWidget(rightVSplitter);
    widgetHSplitter->setStretchFactor(1, 1);

    auto widgetLayout = new QHBoxLayout(this);
    widgetLayout->addWidget(widgetHSplitter);

    // XXX: doing internal sampling and replotting as a base for testing
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=]() {
        sample();
        update();
    });

    timer->setInterval(1000.0);
    timer->start();

    // Create the tree structure containing RateMonitorEntries which in turn
    // point to the samplers stored in m_d
    m_d->createRateMonitorTree(m_d->m_rootNode);

    // Populate the left side tree
    m_d->repopulateTreeWidget();
}

RateMonitorWidget::~RateMonitorWidget()
{
}

void RateMonitorWidget::sample()
{
    m_d->doInternalSampling();
}

void RateMonitorWidget::update()
{
    m_d->m_plotWidget->replot();
}
