#include "replay_ui.h"
#include "replay_ui_p.h"

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTableView>
#include <QTreeView>

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
};

FileInfo gather_fileinfo(QUrl url)
{
    FileInfo result = {};

    try
    {
        result.fileUrl = url;
        qDebug() << __PRETTY_FUNCTION__ << url.path();
        //result.handle = std::move(open_listfile(url.path()));
        //auto [vmeConfig, ec] = read_vme_config_from_listfile(result.handle);
        //result.vmeConfig = std::move(vmeConfig);
        //result.errorCode = ec;
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

using FileInfoPtr = std::shared_ptr<FileInfo>;

FileInfoPtr gather_fileinfo_p(const QUrl &url)
{
    qDebug() << __PRETTY_FUNCTION__ << url.path();
    return std::make_shared<FileInfo>(std::move(gather_fileinfo(url)));
}

struct GatherFunc
{
    using result_type = FileInfoPtr;

    result_type operator() (const QUrl &url)
    {
        qDebug() << __PRETTY_FUNCTION__ << url.path();
        return std::make_shared<FileInfo>(std::move(gather_fileinfo(url)));
    }
};

QFuture<FileInfoPtr> gather_fileinfos(std::vector<QUrl> urls)
{
    auto wrap = []
    {
        std::vector<QUrl> urls = { QUrl("foo"), QUrl("bar") };
        qDebug() << __PRETTY_FUNCTION__ << urls;
        return QtConcurrent::mapped(urls, GatherFunc());
    };

    qDebug() << __PRETTY_FUNCTION__ << urls;
    auto result = wrap();

    return result;
}

}

struct ReplayWidget::Private
{
    ReplayWidget *q = nullptr;
    std::unique_ptr<Ui::ReplayWidget> ui;
    QFileSystemModel *model_browseFs_ = nullptr;
    BrowseFilterModel *model_browseFsProxy_ = nullptr;
    QueueTableModel *model_queue_ = nullptr;
    QFutureWatcher<replay::FileInfoPtr> gatherFileInfoWatcher_;
    QMap<QUrl, replay::FileInfoPtr> fileInfoCache_;

    void startFileInfoGather()
    {
        gatherFileInfoWatcher_.cancel();
        gatherFileInfoWatcher_.setFuture(replay::gather_fileinfos(q->getQueueContents()));
    }

    void onQueueChanged()
    {
        qDebug() << __PRETTY_FUNCTION__ << q->getQueueContents();
        startFileInfoGather();
    }

    void onGatherFileInfoFinished()
    {
        auto f = gatherFileInfoWatcher_.future();

        for (int i=0; i<f.resultCount(); ++i)
        {
            auto fileInfo = f.resultAt(i);
            fileInfoCache_[fileInfo->fileUrl] = fileInfo;
        }

        qDebug() << __PRETTY_FUNCTION__ << fileInfoCache_.keys();
    }
};

ReplayWidget::ReplayWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->ui = std::make_unique<Ui::ReplayWidget>();
    d->ui->setupUi(this);

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
    tb_queueReplay->addAction(action_queueClear);

    auto tb_queueMerge = make_toolbar();
    make_hbox<0, 0>(d->ui->stack_playToolbarsMerge)->addWidget(tb_queueMerge);
    tb_queueMerge->addAction(action_queueStart);
    tb_queueMerge->addAction(action_queuePause);
    tb_queueMerge->addAction(action_queueStop);
    tb_queueMerge->addAction(action_queueClear);

    connect(action_queueClear, &QAction::triggered,
            this, [this] { d->model_queue_->clearQueueContents(); });

    connect(d->model_queue_, &QAbstractItemModel::rowsInserted,
            this, [this] { d->onQueueChanged(); });

    connect(d->model_queue_, &QAbstractItemModel::modelReset,
            this, [this] { d->onQueueChanged(); });

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

std::vector<QUrl> ReplayWidget::getQueueContents() const
{
    return d->model_queue_->getQueueContents();
}

}