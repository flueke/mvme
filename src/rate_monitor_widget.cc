#include "rate_monitor_widget.h"

#include <QBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVector>

#include "util/tree.h"
#include "util/bihash.h"

using RateMonitorNode = util::tree::Node<std::shared_ptr<RateMonitorEntry>>;

struct RateMonitorWidgetPrivate
{
    QTreeWidget *m_rateTree;
    QGroupBox *m_propertyBox;

    QTableWidget *m_rateTable;
    RateMonitorPlotWidget *m_plotWidget;

    MVMEContext *m_context;

    QVector<RateSampler> m_entries;

    DAQStatsSampler m_daqStatsSampler;
    StreamProcessorSampler m_streamProcSampler;
    SIS3153Sampler m_sisReadoutSampler;

    // rate table stuff. TODO: move this to a RateTable abstraction at some point
    void addRateEntryToTable(RateSampler *entry, const QString &name);
    void removeRateEntryFromTable(RateSampler *entry);
    void updateRateTable();
    BiHash<RateSampler *, QTableWidgetItem *> m_rateTableHash; // FIXME: one item for each cell, not one item for a row!

    void dev_test_setup_thingy();
};

void RateMonitorWidgetPrivate::dev_test_setup_thingy()
{
    static const size_t RateHistorySampleCapacity = 60 * 5;

    m_streamProcSampler.bytesProcessed.rateHistory = std::make_shared<RateHistoryBuffer>(RateHistorySampleCapacity);
    m_streamProcSampler.bytesProcessed.scale = 1.0 / Kilobytes(1.0);
    m_plotWidget->addRate(m_streamProcSampler.bytesProcessed.rateHistory, "streamProc.bytes", Qt::black);

    m_streamProcSampler.buffersProcessed.rateHistory = std::make_shared<RateHistoryBuffer>(RateHistorySampleCapacity);
    m_plotWidget->addRate(m_streamProcSampler.buffersProcessed.rateHistory, "streamProc.buffers", Qt::red);
}

void RateMonitorWidgetPrivate::addRateEntryToTable(RateSampler *entry, const QString &name)
{
    assert(!m_rateTableHash.map.contains(entry));

    auto item = new QTableWidgetItem(name);
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

    m_d->m_rateTree = new QTreeWidget;
    m_d->m_rateTable = new QTableWidget;
    m_d->m_plotWidget = new RateMonitorPlotWidget;
    m_d->m_plotWidget->setXAxisReversed(true);
    m_d->m_propertyBox = new QGroupBox("Properties");

    m_d->m_context = context;

    // rate tree setup
    {
        auto tree = m_d->m_rateTree;
        tree->header()->setVisible(true);
        tree->setColumnCount(3);
        tree->setHeaderLabels({"Object", "View", "Plot"});
    }

    // rate table setup
    {
        auto table = m_d->m_rateTable;
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({"Name", "Rate", "Value"});
    }

    auto leftVSplitter = new QSplitter(Qt::Vertical);
    leftVSplitter->addWidget(m_d->m_rateTree);
    leftVSplitter->addWidget(m_d->m_propertyBox);
    leftVSplitter->setStretchFactor(0, 1);

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

    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &RateMonitorWidget::sample);
    timer->setInterval(1000.0);
    timer->start();

    m_d->dev_test_setup_thingy();
}

RateMonitorWidget::~RateMonitorWidget()
{
}

void RateMonitorWidget::sample()
{
    qDebug() << __PRETTY_FUNCTION__ << "sample";

    m_d->m_daqStatsSampler.sample(m_d->m_context->getDAQStats());
    m_d->m_streamProcSampler.sample(m_d->m_context->getMVMEStreamWorker()->getStreamProcessor()->getCounters());

    if (auto rdoWorker = qobject_cast<SIS3153ReadoutWorker *>(m_d->m_context->getReadoutWorker()))
    {
        m_d->m_sisReadoutSampler.sample(rdoWorker->getCounters());
    }

    update();
}

void RateMonitorWidget::update()
{
    m_d->m_plotWidget->replot();
    m_d->updateRateTable();
}
