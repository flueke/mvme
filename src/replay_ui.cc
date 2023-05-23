#include "replay_ui.h"
#include "replay_ui_p.h"

#include <QTableView>
#include <QTreeView>

#include "qt_util.h"
#include "ui_replay_widget.h"
#include "util/qt_str.h"

namespace mesytec::mvme
{

struct ReplayWidget::Private
{
    ReplayWidget *q = nullptr;
    std::unique_ptr<Ui::ReplayWidget> ui;
    QFileSystemModel *model_browseFs_ = nullptr;
    BrowseFilterModel *model_browseFsProxy_ = nullptr;
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
    //d->ui->stack_playToolbarsReplay->setLayout(
    //d->ui->stack_playToolbarsReplay->addAction(new QAction("foobar"));
    {
        auto layout = make_hbox<0,0>(d->ui->stack_playToolbarsReplay);
        auto tb = make_toolbar();
        layout->addWidget(tb);
        tb->addAction("start");
        tb->addAction("pause");
        tb->addAction("stop");
        //tb->addAction("start");
    }
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

}