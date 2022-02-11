#include "multi_crate_mainwindow.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QWindow>
#include <QScrollArea>

#include "qt_util.h"
#include "vme_config_tree.h"

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
    WidgetGeometrySaver *geometrySaver;

    std::vector<VMEConfigTreeWidget *> crateConfigWidgets;
    std::vector<std::shared_ptr<VMEConfig>> crateConfigs;
};

MultiCrateMainWindow::MultiCrateMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , d(std::make_unique<Private>(this))
{
    setObjectName("mvme_multi_crate_mainwindow");
    setWindowTitle("mvme multi crate");

    d->centralWidget          = new QWidget(this);
    d->centralLayout          = new QHBoxLayout(d->centralWidget);
    d->statusBar              = new QStatusBar(this);
    d->statusBar->setSizeGripEnabled(false);
    d->menuBar                = new QMenuBar(this);
    d->geometrySaver          = new WidgetGeometrySaver(this);

    setCentralWidget(d->centralWidget);
    setStatusBar(d->statusBar);
    setMenuBar(d->menuBar);

    auto menu = d->menuBar->addMenu("&File");
    auto action = menu->addAction("&Quit", this, [this] { close(); });
    action->setShortcut(QSL("Ctrl+Q"));
    action->setShortcutContext(Qt::ApplicationShortcut);

    auto containerWidget = new QWidget;
    auto containerLayout = new QHBoxLayout(containerWidget);

    for (int i=0; i<3; ++i)
    {
        auto w = new VMEConfigTreeWidget();
        containerLayout->addWidget(w);
        //d->centralLayout->addWidget(w);
        d->crateConfigWidgets.push_back(w);
        auto crateConfig = std::make_shared<VMEConfig>();
        d->crateConfigs.push_back(crateConfig);
        w->setConfig(crateConfig.get());
    }

    auto scrollArea = new QScrollArea;
    //scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(containerWidget);
    d->centralLayout->addWidget(scrollArea);
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
