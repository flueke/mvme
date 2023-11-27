#include "multi_crate_mainwindow.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QWindow>
#include <QScrollArea>

#include "multi_crate.h"
#include "qt_util.h"
#include "util/qt_model_view_util.h"
#include "vme_config_model_view.h"
#include "vme_config_tree.h"

namespace mesytec::mvme
{

struct MultiCrateMainWindow::Private
{
    Private(MultiCrateMainWindow *q_)
        : q(q_)
    { }

    MultiCrateMainWindow *q;
    QWidget *centralWidget;
    QBoxLayout *centralLayout;
    QStatusBar *statusBar;
    QMenuBar *menuBar;
    QToolBar *configToolBar;
    QToolBar *daqToolBar;
    QLineEdit *le_filename;
    WidgetGeometrySaver *geometrySaver;

    VmeConfigItemModel *vmeConfigModel_;
    VmeConfigItemController *vmeConfigController_;
    QTreeView *vmeConfigView_;
    std::shared_ptr<ConfigObject> vmeConfig_;
    QString vmeConfigFilename_;

    void onViewItemDoubleClicked(const QModelIndex &index)
    {
        if (auto item = vmeConfigModel_->itemFromIndex(index))
        {
            if (auto config = qobject_from_item<VMEScriptConfig>(item))
                emit q->editVmeScript(config);
        }
    }
};

MultiCrateMainWindow::MultiCrateMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , d(std::make_unique<Private>(this))
{
    setObjectName("mvme_multi_crate_mainwindow");
    setWindowTitle("mvme multi crate");

    d->centralWidget          = new QWidget(this);
    d->centralLayout          = new QVBoxLayout(d->centralWidget);
    d->statusBar              = new QStatusBar(this);
    d->statusBar->setSizeGripEnabled(false);
    d->menuBar                = new QMenuBar(this);
    d->configToolBar = new QToolBar(this);
    d->configToolBar->setObjectName("VmeConfigToolBar");
    d->configToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    d->daqToolBar = new QToolBar(this);
    d->daqToolBar->setObjectName("DaqToolBar");
    d->daqToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    d->le_filename = new QLineEdit;
    d->le_filename->setReadOnly(true);
    d->geometrySaver          = new WidgetGeometrySaver(this);

    setCentralWidget(d->centralWidget);
    setStatusBar(d->statusBar);
    setMenuBar(d->menuBar);
    addToolBar(d->configToolBar);
    addToolBar(d->daqToolBar);

    d->vmeConfigModel_ = new VmeConfigItemModel(this);
    d->vmeConfigController_ = new VmeConfigItemController(this);
    d->vmeConfigView_ = new VmeConfigTreeView;
    d->vmeConfigView_->setContextMenuPolicy(Qt::CustomContextMenu);
    d->vmeConfigController_->setModel(d->vmeConfigModel_);
    d->vmeConfigController_->addView(d->vmeConfigView_);

    auto scrollArea = new QScrollArea;
    //scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(d->vmeConfigView_);

    d->centralLayout->addWidget(d->le_filename);
    d->centralLayout->addWidget(scrollArea);

    auto menuFile = d->menuBar->addMenu("&File");

    auto actionNewVMEConfig     = new QAction(QIcon(QSL(":/document-new.png")), QSL("New VME Config"), this);
    actionNewVMEConfig->setToolTip(QSL("New VME Config"));
    actionNewVMEConfig->setIconText(QSL("New"));
    actionNewVMEConfig->setObjectName(QSL("actionNewVMEConfig"));
    actionNewVMEConfig->setShortcut(QSL("Ctrl+N"));

    auto actionOpenVMEConfig    = new QAction(QIcon(QSL(":/document-open.png")), QSL("Open VME Config"), this);
    actionOpenVMEConfig->setObjectName(QSL("actionOpenVMEConfig"));
    actionOpenVMEConfig->setToolTip(QSL("Open VME Config"));
    actionOpenVMEConfig->setIconText(QSL("Open"));
    actionOpenVMEConfig->setShortcut(QSL("Ctrl+O"));

    auto actionSaveVMEConfig    = new QAction(QIcon(QSL(":/document-save.png")), QSL("Save VME Config"), this);
    actionSaveVMEConfig->setObjectName(QSL("actionSaveVMEConfig"));
    actionSaveVMEConfig->setToolTip(QSL("Save VME Config"));
    actionSaveVMEConfig->setIconText(QSL("Save"));
    actionSaveVMEConfig->setShortcut(QSL("Ctrl+S"));

    auto actionSaveVMEConfigAs  = new QAction(QIcon(QSL(":/document-save-as.png")), QSL("Save VME Config As"), this);
    actionSaveVMEConfigAs->setObjectName(QSL("actionSaveVMEConfigAs"));
    actionSaveVMEConfigAs->setToolTip(QSL("Save VME Config As"));
    actionSaveVMEConfigAs->setIconText(QSL("Save As"));

    auto actionExploreWorkspace  = new QAction(QIcon(QSL(":/folder_orange.png")), QSL("Explore Workspace"), this);
    actionExploreWorkspace->setObjectName(QSL("actionExploreWorkspace"));
    actionExploreWorkspace->setToolTip(QSL("Open workspace directory in file manager"));
    actionExploreWorkspace->setIconText(QSL("Explore Workspace"));

    auto actionReloadView = new QAction("Reload View");

    auto actionQuit = menuFile->addAction("&Quit", this, [this] { close(); });
    actionQuit->setShortcut(QSL("Ctrl+Q"));
    actionQuit->setShortcutContext(Qt::ApplicationShortcut);

    d->configToolBar->addAction(actionNewVMEConfig);
    d->configToolBar->addAction(actionOpenVMEConfig);
    d->configToolBar->addAction(actionSaveVMEConfig);
    d->configToolBar->addAction(actionSaveVMEConfigAs);
    d->configToolBar->addAction(actionExploreWorkspace);
    d->configToolBar->addAction(actionReloadView);

    connect(actionNewVMEConfig, &QAction::triggered, this, &MultiCrateMainWindow::newVmeConfig);
    connect(actionOpenVMEConfig, &QAction::triggered, this, &MultiCrateMainWindow::openVmeConfig);
    connect(actionSaveVMEConfig, &QAction::triggered, this, &MultiCrateMainWindow::saveVmeConfig);
    connect(actionSaveVMEConfigAs, &QAction::triggered, this, &MultiCrateMainWindow::saveVmeConfigAs);
    connect(actionExploreWorkspace, &QAction::triggered, this, &MultiCrateMainWindow::exploreWorkspace);
    connect(actionReloadView, &QAction::triggered, this, &MultiCrateMainWindow::reloadView);

    auto actionDaqStart    = new QAction(QIcon(":/control_play.png"), QSL("Start DAQ"), this);
    actionDaqStart->setObjectName(QSL("actionDaqStart"));
    auto actionDaqStop     = new QAction(QIcon(":/control_stop_square.png"), QSL("Stop DAQ"), this);
    actionDaqStop->setObjectName(QSL("actionDaqStop"));
    auto actionDaqPause    = new QAction(QIcon(":/control_pause.png"), QSL("Pause DAQ"), this);
    actionDaqPause->setObjectName(QSL("actionDaqPause"));
    auto actionDaqResume   = new QAction(QIcon(":/control_play.png"), QSL("Resume DAQ"), this);
    actionDaqResume->setObjectName(QSL("actionDaqResume"));

    d->daqToolBar->addAction(actionDaqStart);
    d->daqToolBar->addAction(actionDaqStop);
    d->daqToolBar->addAction(actionDaqPause);
    d->daqToolBar->addAction(actionDaqResume);

    connect(actionDaqStart, &QAction::triggered, this, &MultiCrateMainWindow::startDaq);
    connect(actionDaqStop, &QAction::triggered, this, &MultiCrateMainWindow::stopDaq);
    connect(actionDaqPause, &QAction::triggered, this, &MultiCrateMainWindow::pauseDaq);
    connect(actionDaqResume, &QAction::triggered, this, &MultiCrateMainWindow::resumeDaq);

    connect(d->vmeConfigView_, &QTreeView::doubleClicked,
        this, [this] (const QModelIndex &index) { d->onViewItemDoubleClicked(index); });

    connect(d->vmeConfigView_, &QTreeView::customContextMenuRequested,
        this, &MultiCrateMainWindow::vmeTreeContextMenuRequested);
}

MultiCrateMainWindow::~MultiCrateMainWindow()
{
}

void MultiCrateMainWindow::closeEvent(QCloseEvent *event)
{
    bool allWindowsClosed = true;

    for (auto window: QGuiApplication::topLevelWindows())
    {
        if (window != this->windowHandle())
        {
            if (!window->close())
            {
                window->raise();
                allWindowsClosed = false;
                break;
            }
        }
    }

    if (!allWindowsClosed)
    {
        event->ignore();
        return;
    }

    // window sizes and positions
    QSettings settings;
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());

    QMainWindow::closeEvent(event);

    if (auto app = qobject_cast<QApplication *>(qApp))
        app->closeAllWindows();
}

void MultiCrateMainWindow::setConfig(const std::shared_ptr<ConfigObject> &config, const QString &filename)
{
    d->vmeConfig_ = config;
    d->vmeConfigFilename_ = filename;

    d->vmeConfigModel_->setRootObject(d->vmeConfig_.get());

    if (d->vmeConfig_ && d->vmeConfigModel_->invisibleRootItem()->child(0))
    {
        d->vmeConfigView_->setRootIndex(d->vmeConfigModel_->invisibleRootItem()->child(0)->index());
    }

    if (d->vmeConfigFilename_.isEmpty())
        d->le_filename->setText("<unsaved>");
    else
        d->le_filename->setText(d->vmeConfigFilename_);
}

std::shared_ptr<ConfigObject> MultiCrateMainWindow::getConfig()
{
    return d->vmeConfig_;
}

void MultiCrateMainWindow::setConfigFilename(const QString &filename)
{
    d->vmeConfigFilename_ = filename;
    d->le_filename->setText(d->vmeConfigFilename_);
}

QString MultiCrateMainWindow::getConfigFilename() const
{
    return d->vmeConfigFilename_;
}

QTreeView *MultiCrateMainWindow::getVmeConfigTree()
{
    return d->vmeConfigView_;
}

VmeConfigItemModel *MultiCrateMainWindow::getVmeConfigModel()
{
    return d->vmeConfigModel_;
}

VmeConfigItemController *MultiCrateMainWindow::getVmeConfigItemController()
{
    return d->vmeConfigController_;
}

void MultiCrateMainWindow::reloadView()
{
    if (d->vmeConfig_)
    {
        d->vmeConfigModel_->setRootObject(d->vmeConfig_.get());
        d->vmeConfigView_->setRootIndex(d->vmeConfigModel_->invisibleRootItem()->child(0)->index());
    }
}

}
