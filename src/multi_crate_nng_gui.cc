#include "multi_crate_nng_gui.h"
#include "qt_util.h"
#include "util/qt_model_view_util.h"
#include "util/qt_monospace_textedit.h"

namespace mesytec::mvme::multi_crate
{

enum ItemType
{
    Unspecified = QStandardItem::UserType + 1,
    Pipeline,
    Step
};

enum DataRole
{
    PipelineName = Qt::UserRole + 1,
};

static const QStringList HeaderLabels = { "Object", "State", "Msgs", "Msg Rate [msgs/s]", "Data [MiB]", "Data Rate [MiB/s]", "Message Loss [msgs]" };

QList<QStandardItem *> make_row(size_t columns, int type = QStandardItem::UserType + 1)
{
    QList<QStandardItem *> row;

    for (size_t i=0; i<columns; i++)
        row.append(new BaseItem(type));

    return row;
}

QList<QStandardItem *> build_step(const CratePipelineStep &step)
{
    auto item = ItemBuilder(ItemType::Step, step.context->name().c_str()).build();
    item->setData(step.context->name().c_str(), DataRole::PipelineName);

    auto item1 = ItemBuilder(ItemType::Unspecified, "state").build();

    if (step.context->inputReader())
    {
        auto row = make_row(HeaderLabels.size());
        row[0]->setText("rx");
        item->appendRow(row);
    }

    if (step.context->outputWriter())
    {
        auto row = make_row(HeaderLabels.size());
        row[0]->setText("tx");
        item->appendRow(row);
    }

    return { item, item1 };
}

void update_step(std::vector<QStandardItem *> roots, const CratePipelineStep &step)
{
    assert(roots.size() == 2);

    const bool isRunning = step.context->jobRuntime().isRunning();
    std::string str("idle");

    if (isRunning)
        str = "running";
    else if (step.context->lastResult())
        str = fmt::format("idle, result={}", step.context->lastResult()->toString());

    roots[1]->setText(QString::fromStdString(str));

    size_t row = 0;

    if (step.context->inputReader())
    {
        auto counters = step.context->readerCounters().copy();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            (isRunning ?  std::chrono::steady_clock::now() : counters.tpStop) - counters.tpStart);
        auto msgs = counters.messagesReceived;
        auto mib = counters.bytesReceived * 1.0 / mvlc::util::Megabytes(1);
        auto msgRate = counters.messagesReceived * 1.0 / elapsed.count() * 1000.0;
        auto mibRate = mib / elapsed.count() * 1000.0;

        size_t col = 2;

        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(msgs));
        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(msgRate, 'f', 2));
        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(mib, 'f', 2));
        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(mibRate, 'f', 2));
        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(counters.messagesLost));

        ++row;
    }

    if (step.context->outputWriter())
    {
        auto counters = step.context->writerCounters().copy();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            (isRunning ?  std::chrono::steady_clock::now() : counters.tpStop) - counters.tpStart);
        auto msgs = counters.messagesSent;
        auto mib = counters.bytesSent * 1.0 / mvlc::util::Megabytes(1);
        auto msgRate = counters.messagesSent * 1.0 / elapsed.count() * 1000.0;
        auto mibRate = mib / elapsed.count() * 1000.0;

        size_t col = 2;

        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(msgs));
        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(msgRate, 'f', 2));
        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(mib, 'f', 2));
        if (auto item = roots[0]->child(row, col++)) item->setText(QString::number(mibRate, 'f', 2));

        ++row;
    }
}

QList<QStandardItem *> build_pipeline(const std::string &name, const CratePipeline &pipeline)
{
    auto root = ItemBuilder(ItemType::Pipeline, name.c_str()).build();
    root->setData(name.c_str(), DataRole::PipelineName);

    auto root1 = ItemBuilder(ItemType::Unspecified, "state").build();

    for (auto &step: pipeline)
    {
        auto row = build_step(step);
        root->appendRow(row);
    }

    return { root, root1 };
}

void update_pipeline(std::vector<QStandardItem *> roots, const CratePipeline &pipeline)
{
    assert(roots.size() == 2);

    //const bool allIdle = std::all_of(std::begin(pipeline), std::end(pipeline), [] (const auto &step) { return !step.context->jobRuntime().isRunning(); });
    const bool anyRunning = std::any_of(std::begin(pipeline), std::end(pipeline), [] (const auto &step) { return step.context->jobRuntime().isRunning(); });

    roots[1]->setText(!anyRunning ? "idle" : "running");

    assert(static_cast<size_t>(roots[0]->rowCount()) == pipeline.size());

    for (size_t i=0; i<pipeline.size(); i++)
    {
        auto &step = pipeline[i];
        auto stepRoots = collect_children(roots[0], i);
        update_step(stepRoots, step);
    }
}

std::vector<QList<QStandardItem *>> build_pipelines(const std::map<std::string, CratePipeline> &pipelines)
{
    std::vector<QList<QStandardItem *>> result;

    for (auto &[name, pipeline]: pipelines)
    {
        result.emplace_back(build_pipeline(name, pipeline));
    }

    return result;
}

void update_pipelines(QStandardItem *root, const std::map<std::string, CratePipeline> &pipelines)
{
    assert(static_cast<size_t>(root->rowCount()) == pipelines.size());

    for (auto &[name, pipeline]: pipelines)
    {
        auto p = [name=name] (QStandardItem *item)
        {
            return item->type() == ItemType::Pipeline && item->data(DataRole::PipelineName) == QString::fromStdString(name);
        };

        auto pipelineNodes = find_items(root, p);
        assert(pipelineNodes.size() == 1);
        auto pipelineRoots = collect_children(root, pipelineNodes[0]->row());

        update_pipeline(pipelineRoots, pipeline);
    }
}

struct CratePipelineMonitorWidget::Private
{
    CratePipelineMonitorWidget *q = nullptr;
    QTimer refreshTimer;
    QTreeView *treeView = nullptr;
    std::map<std::string, CratePipeline> pipelines_;
    QStandardItemModel model;

    explicit Private(CratePipelineMonitorWidget *q_)
        : q(q_)
    {
        auto l = make_hbox(q);
        treeView = new QTreeView;
        l->addWidget(treeView);

        treeView->setModel(&model);

        QObject::connect(&refreshTimer, &QTimer::timeout, q, [this](){ refresh(); });
        refreshTimer.setInterval(500);
        refreshTimer.start();
    }

    void rebuild()
    {
        model.clear();
        model.setItemPrototype(new BaseItem);
        model.setHorizontalHeaderLabels(HeaderLabels);
        auto rows = build_pipelines(pipelines_);
        for (auto row: rows)
        {
            model.invisibleRootItem()->appendRow(row);
            if (!rows.empty())
            {
                treeView->setExpanded(model.indexFromItem(row[0]), true);
            }
        }
        auto headerView = treeView->header();
        treeView->resizeColumnToContents(0);

        for (int section=0; section<headerView->count(); section++)
        {
            if (headerView->sectionSize(section) < headerView->sectionSizeHint(section))
                headerView->resizeSection(section, headerView->sectionSizeHint(section));
        }

        auto items = find_items(model.invisibleRootItem(), [] (QStandardItem *item) { return item->type() == ItemType::Step; });
        for (auto item: items)
            treeView->setExpanded(model.indexFromItem(item), true);
    }

    void refresh()
    {
        update_pipelines(model.invisibleRootItem(), pipelines_);
    }
};

CratePipelineMonitorWidget::CratePipelineMonitorWidget(QWidget* parent)
    : QWidget(parent)
    , d(std::make_unique<Private>(this))
{
}

CratePipelineMonitorWidget::~CratePipelineMonitorWidget() {}

void CratePipelineMonitorWidget::addPipeline(const std::string &name, const std::vector<CratePipelineStep> &pipeline)
{
    d->pipelines_[name] = pipeline;
    d->rebuild();
}

void CratePipelineMonitorWidget::removePipeline(const std::string &name)
{
    d->pipelines_.erase(name);
    d->rebuild();
}


}
