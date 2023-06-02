#include "replay_ui.h"
#include "replay_ui_p.h"

#include <QFutureWatcher>
#include <QSet>
#include <QStatusBar>
#include <QTableView>
#include <QThread>
#include <QTreeView>

#include <algorithm>
#include <chrono>

#include <mesytec-mvlc/util/protected.h>

#include "qt_util.h"
#include "ui_replay_widget.h"

using mesytec::mvlc::WaitableProtected;

namespace mesytec::mvme
{

namespace replay
{

struct ListfileCommandExecutor::Private
{
    WaitableProtected<State> state_ = State::Idle;
    CommandHolder cmd_;

    std::thread runThread_;
    void run();
};

ListfileCommandExecutor::ListfileCommandExecutor(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{}

ListfileCommandExecutor::~ListfileCommandExecutor()
{}

ListfileCommandExecutor::State ListfileCommandExecutor::state() const
{
    return d->state_.copy();
}

bool ListfileCommandExecutor::setCommand(const CommandHolder &cmd)
{
    if (auto access = d->state_.access(); access.ref() == State::Idle)
    {
        d->cmd_ = cmd;
        return true;
    }

    return false;
}

void ListfileCommandExecutor::start()
{
    if (auto access = d->state_.access();
        access.ref() == State::Idle && d->cmd_.index() != std::variant_npos)
    {
        d->runThread_ = std::thread(&Private::run, d.get());
        access.ref() = State::Running;
    }
}

void ListfileCommandExecutor::cancel()
{
}

void ListfileCommandExecutor::pause()
{
}

void ListfileCommandExecutor::resume()
{
}

void ListfileCommandExecutor::skip()
{
}

void ListfileCommandExecutor::Private::run()
{
    if (cmd_.index() == ReplayCommandType::Replay)
    {
        auto cmd = std::get<0>(cmd_);
        const auto queueSize = cmd.queue.size();

        for (int i=0; i<queueSize; ++i)
        {
            auto url = cmd.queue[i];
        }
    }
}

}

struct ReplayWidget::Private
{
    ReplayWidget *q = nullptr;
    std::unique_ptr<Ui::ReplayWidget> ui;
    QStatusBar *statusbar_ = nullptr;

    QFileSystemModel *model_browseFs_ = nullptr;
    BrowseFilterModel *model_browseFsProxy_ = nullptr;
    QueueTableModel *model_queue_ = nullptr;

    QTimer startGatherFileInfoTimer_;
    QFutureWatcher<QVector<replay::FileInfoPtr>> gatherFileInfoWatcher_;
    std::shared_ptr<replay::FileInfoCache> fileInfoCache_;

    QVector<QUrl> urlsMissingFromInfoCache(const QVector<QUrl> &inputUrls)
    {
        QVector<QUrl> stillMissing;

        for (const auto &url: inputUrls)
            if (!fileInfoCache_->contains(url))
                stillMissing.push_back(url);

        return stillMissing;
    }

    void startGatherFileInfo(const QVector<QUrl> &urls)
    {
        if (gatherFileInfoWatcher_.isFinished())
        {
            //qDebug() << __PRETTY_FUNCTION__ << "start new gather for" << urls.size() << "urls";
            // Wrap the blocking call to gater_fileinfos() in
            // QtConcurrent::run(). This works and does not have the memory
            // management issues QtConcurrent::mapped() has, as all arguments to
            // run() are copied to the target thread.
            auto f = QtConcurrent::run(replay::gather_fileinfos, urls);
            gatherFileInfoWatcher_.setFuture(f);
            statusbar_->showMessage("gathering file info...");
        }
    }

    void startGatherMissingFileInfos()
    {
        if (auto missing = urlsMissingFromInfoCache(q->getQueueContents()); !missing.isEmpty())
            startGatherFileInfo(missing);
    }

    void onGatherFileInfoFinished()
    {
        statusbar_->clearMessage();

        auto results = gatherFileInfoWatcher_.future().result();

        for (const auto &fileInfo: results)
            (*fileInfoCache_)[fileInfo->fileUrl] = fileInfo;

        //qDebug() << __PRETTY_FUNCTION__ << "new cache size =" << fileInfoCache_->size();

        startGatherMissingFileInfos();

        model_queue_->setFileInfoCache(fileInfoCache_);
    }

    void onQueueChanged()
    {
        startGatherFileInfoTimer_.start(std::chrono::milliseconds(500));
    }
};

ReplayWidget::ReplayWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->ui = std::make_unique<Ui::ReplayWidget>();
    d->ui->setupUi(this);
    d->statusbar_ = new QStatusBar;
    d->ui->outerLayout->addWidget(d->statusbar_);
    d->fileInfoCache_ = std::make_shared<replay::FileInfoCache>();

    // browsing
    d->model_browseFs_ = new QFileSystemModel(this);
    d->model_browseFs_->setReadOnly(true);
    d->model_browseFs_->setFilter(QDir::NoDotAndDotDot | QDir::Files | QDir::AllDirs);
    d->model_browseFs_->setNameFilterDisables(true);
    d->model_browseFsProxy_ = new BrowseFilterModel(this);
    d->model_browseFsProxy_->setSourceModel(d->model_browseFs_);
    d->model_browseFsProxy_->setFilterRegExp(QRegExp("*.zip", Qt::CaseInsensitive, QRegExp::WildcardUnix));
    d->model_browseFsProxy_->setDynamicSortFilter(true);
    d->model_browseFsProxy_->setRecursiveFilteringEnabled(true);

    d->ui->tree_filesystem->setModel(d->model_browseFsProxy_);
    d->ui->tree_filesystem->hideColumn(2); // Hides the "file type" column
    d->ui->tree_filesystem->setSortingEnabled(true);
    d->ui->tree_filesystem->sortByColumn(0, Qt::AscendingOrder);
    d->ui->tree_filesystem->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->ui->tree_filesystem->setDragEnabled(true);

    // queue
    d->model_queue_ = new QueueTableModel(this);

    d->ui->table_queue->setModel(d->model_queue_);
    d->ui->table_queue->setAcceptDrops(true);
    d->ui->table_queue->setDefaultDropAction(Qt::MoveAction);
    d->ui->table_queue->setDragDropMode(QAbstractItemView::DragDrop);
    d->ui->table_queue->setDragEnabled(true);
    d->ui->table_queue->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->ui->table_queue->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->ui->table_queue->setDragDropOverwriteMode(false);
    d->ui->table_queue->setStyle(new FullWidthDropIndicatorStyle(style()));
    d->ui->table_queue->verticalHeader()->hide();
    d->ui->table_queue->horizontalHeader()->setHighlightSections(false);
    d->ui->table_queue->horizontalHeader()->setStretchLastSection(true);

    // queue actions and toolbars
    auto action_queueStart = new QAction("start", this);
    auto action_queuePause = new QAction("pause", this);
    auto action_queueStop = new QAction("stop", this);
    auto action_queueSkip = new QAction("skip", this);
    auto action_queueClear = new QAction("clear", this);

    auto tb_queueReplay = make_toolbar();
    make_hbox<0, 0>(d->ui->stack_playToolbarsReplay)->addWidget(tb_queueReplay);
    tb_queueReplay->addAction(action_queueStart);
    tb_queueReplay->addAction(action_queuePause);
    tb_queueReplay->addAction(action_queueStop);
    tb_queueReplay->addAction(action_queueSkip);
    tb_queueReplay->addSeparator();
    tb_queueReplay->addAction(action_queueClear);

    auto tb_queueMerge = make_toolbar();
    make_hbox<0, 0>(d->ui->stack_playToolbarsMerge)->addWidget(tb_queueMerge);
    tb_queueMerge->addAction(action_queueStart);
    tb_queueMerge->addAction(action_queuePause);
    tb_queueMerge->addAction(action_queueStop);
    tb_queueMerge->addSeparator();
    tb_queueMerge->addAction(action_queueClear);

    connect(action_queueStart, &QAction::triggered, this, &ReplayWidget::start);
    connect(action_queueStop, &QAction::triggered, this, &ReplayWidget::stop);
    connect(action_queuePause, &QAction::triggered, this, &ReplayWidget::pause);
    //connect(action_queueClear, &QAction::triggered, this, &ReplayWidget::resume);
    connect(action_queueSkip, &QAction::triggered, this, &ReplayWidget::skip);

    connect(action_queueClear, &QAction::triggered,
            this, [this] { d->model_queue_->clearQueueContents(); });

    // react to queue changes
    connect(d->model_queue_, &QAbstractItemModel::rowsInserted,
            this, [this] { d->onQueueChanged(); });

    connect(d->model_queue_, &QAbstractItemModel::modelReset,
            this, [this] { d->onQueueChanged(); });

    connect(d->model_queue_, &QStandardItemModel::itemChanged,
            this, [this]
            {
                d->ui->table_queue->resizeColumnsToContents();
                d->ui->table_queue->resizeRowsToContents();
            });

    // file info gather
    d->startGatherFileInfoTimer_.setSingleShot(true);

    connect(&d->startGatherFileInfoTimer_, &QTimer::timeout,
            this, [this] { d->startGatherMissingFileInfos(); });

    connect(&d->gatherFileInfoWatcher_, &QFutureWatcher<replay::FileInfoPtr>::finished,
            this, [this] { d->onGatherFileInfoFinished(); });
}

ReplayWidget::~ReplayWidget()
{
}

void ReplayWidget::browsePath(const QString &path)
{
    d->model_browseFs_->setRootPath(path);
    d->ui->tree_filesystem->setRootIndex(
        d->model_browseFsProxy_->mapFromSource(d->model_browseFs_->index(path)));
}

QString ReplayWidget::getBrowsePath() const
{
    return d->model_browseFs_->rootPath();
}

QVector<QUrl> ReplayWidget::getQueueContents() const
{
    return d->model_queue_->getQueueContents();
}

void ReplayWidget::clearFileInfoCache()
{
    d->fileInfoCache_->clear();
}

replay::CommandHolder ReplayWidget::getCommand() const
{
    const auto mode = static_cast<replay::ReplayCommandType>(d->ui->combo_playMode->currentIndex());

    switch (mode)
    {
        case replay::ReplayCommandType::Replay:
        {
            replay::ReplayCommand ret;
            ret.queue = getQueueContents();
            // FIXME: loads the analysis from the first file. only do this in case no user analysis has been specified.
            if (!ret.queue.isEmpty() && d->fileInfoCache_->contains(ret.queue.front()))
            {
                ret.analysisBlob = d->fileInfoCache_->value(ret.queue.front())->handle.analysisBlob;
                ret.analysisFilename = "<from " + ret.queue.front().fileName() + ">";
            }
            return ret;
        } break;

        case replay::ReplayCommandType::Merge:
        {
            replay::MergeCommand ret;
            ret.queue = getQueueContents();
            return ret;
        } break;

        case replay::ReplayCommandType::Split:
        {
            replay::SplitCommand ret;
            ret.queue = getQueueContents();
            return ret;
        } break;

        case replay::ReplayCommandType::Filter:
        {
            replay::FilterCommand ret;
            ret.queue = getQueueContents();
            return ret;
        } break;
    }

    return {};
}

void ReplayWidget::setCurrentFilename(const QString &filename)
{
}

void ReplayWidget::setRunning()
{
    d->statusbar_->showMessage(QSL("Running"));
}

void ReplayWidget::setIdle()
{
    d->statusbar_->showMessage(QSL("Idle"));
}

void ReplayWidget::setPaused()
{
    d->statusbar_->showMessage(QSL("Paused"));
}

}
