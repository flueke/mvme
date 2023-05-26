#include "replay_ui.h"
#include "replay_ui_p.h"

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QSet>
#include <QStatusBar>
#include <QTableView>
#include <QThread>
#include <QTreeView>

#include <algorithm>
#include <chrono>
#include <thread>

#include "qt_util.h"
#include "listfile_replay.h"
#include "ui_replay_widget.h"

namespace mesytec::mvme
{

namespace replay
{

struct FileInfo
{
    QUrl fileUrl;
    ListfileReplayHandle handle;
    std::unique_ptr<VMEConfig> vmeConfig;
    QString errorString;
    std::error_code errorCode;
    std::exception_ptr exceptionPtr;
    std::chrono::steady_clock::time_point updateTime;
};

FileInfo gather_fileinfo(QUrl url)
{
    FileInfo result = {};

    try
    {
        result.fileUrl = url;
        result.handle = open_listfile(url.path());
        auto [vmeConfig, ec] = read_vme_config_from_listfile(result.handle);
        result.vmeConfig = std::move(vmeConfig);
        result.errorCode = ec;
        result.updateTime = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    }
    catch(const QString &e)
    {
        result.errorString = e;
    }
    catch(const std::exception &)
    {
        result.exceptionPtr = std::current_exception();
    }

    return result;
}

// FileInfo is not copyable but only movable so a shared_ptr to FileInfo is used
// which can be copied freely.
using FileInfoPtr = std::shared_ptr<FileInfo>;

FileInfoPtr gather_fileinfo_p(const QUrl &url)
{
    //qDebug() << __PRETTY_FUNCTION__ << url.path();
    return std::make_shared<FileInfo>(gather_fileinfo(url));
}

// Parallel gather of file infos. Blocks until all gather results are available.
QVector<FileInfoPtr> gather_fileinfos(const QVector<QUrl> &urls)
{
    return QtConcurrent::blockingMapped(urls, gather_fileinfo_p);
}

}

struct ReplayWidget::Private
{
    static const int ModeIndex_Replay = 0;
    static const int ModeIndex_Merge = 1;
    static const int ModeIndex_Split = 2;
    static const int ModeIndex_Filter = 3;

    ReplayWidget *q = nullptr;
    std::unique_ptr<Ui::ReplayWidget> ui;
    QStatusBar *statusbar_ = nullptr;

    QFileSystemModel *model_browseFs_ = nullptr;
    BrowseFilterModel *model_browseFsProxy_ = nullptr;
    QueueTableModel *model_queue_ = nullptr;

    QTimer startGatherFileInfoTimer_;
    QFutureWatcher<QVector<replay::FileInfoPtr>> gatherFileInfoWatcher_;
    QMap<QUrl, replay::FileInfoPtr> fileInfoCache_;

    QVector<QUrl> urlsMissingFromInfoCache(const QVector<QUrl> &inputUrls)
    {
        QVector<QUrl> stillMissing;

        for (const auto &url: inputUrls)
            if (!fileInfoCache_.contains(url))
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
        //qDebug() << __PRETTY_FUNCTION__;

        statusbar_->clearMessage();

        auto results = gatherFileInfoWatcher_.future().result();

        for (const auto &fileInfo: results)
            fileInfoCache_[fileInfo->fileUrl] = fileInfo;

        //qDebug() << __PRETTY_FUNCTION__ << "new cache size =" << fileInfoCache_.size();

        startGatherMissingFileInfos();
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
    d->fileInfoCache_.clear();
}

replay::CommandHolder ReplayWidget::getCommand() const
{
    int modeIndex = d->ui->combo_playMode->currentIndex();

    #if 0
    if (modeIndex == Private::ModeIndex_Replay)
    {
        replay::ReplayCommand ret;
        ret.queue = getQueueContents();
        // FIXME: loads the analysis from the first file. only do this in case no user analysis has been specified.
        if (!ret.queue.isEmpty() && d->fileInfoCache_.contains(ret.queue.front()))
            ret.analysisBlob = d->fileInfoCache_[ret.queue.front()]->handle.analysisBlob;
        return ret;
    }
    else if (modeIndex == Private::ModeIndex_Merge)
    {
        replay::MergeCommand ret;
        return ret;
    }
    #endif

    return {};
}

void ReplayWidget::setCurrentFilename(const QString &filename)
{
}

}
