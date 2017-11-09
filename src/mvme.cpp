/* MVMEMainWindow - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvme.h"

#include "analysis/analysis.h"
#include "analysis/analysis_ui.h"
#include "config_ui.h"
#include "daqcontrol_widget.h"
#include "daqstats_widget.h"
#include "gui_util.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "listfile_browser.h"
#include "mesytec_diagnostics.h"
#include "mvme_context.h"
#include "mvme_event_processor.h"
#include "mvme_listfile.h"
#include "mvme_context_lib.h"
#include "qt_util.h"
#include "sis3153_util.h"
#include "util_zip.h"
#include "vme_config_tree.h"
#include "vme_debug_widget.h"
#include "vme_script_editor.h"
#include "vmusb_firmware_loader.h"

#ifdef MVME_USE_GIT_VERSION_FILE
#include "git_sha1.h"
#endif
#include "build_info.h"

#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QList>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextEdit>
#include <QtGui>
#include <QtNetwork>
#include <QToolBar>
#include <quazipfile.h>
#include <QVBoxLayout>
#include <qwt_plot_curve.h>

using namespace vats;

struct MVMEWindowPrivate
{
    MVMEContext *m_context;
    QWidget *centralWidget = nullptr;
    QVBoxLayout *centralLayout = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    DAQControlWidget *m_daqControlWidget = nullptr;
    VMEConfigTreeWidget *m_vmeConfigTreeWidget = nullptr;
    DAQStatsWidget *m_daqStatsWidget = nullptr;
    VMEDebugWidget *m_vmeDebugWidget = nullptr;
    QMap<QObject *, QList<QWidget *>> m_objectWindows;
    WidgetGeometrySaver *m_geometrySaver;
    QNetworkAccessManager *m_networkAccessManager = nullptr;
    ListfileBrowser *m_listfileBrowser = nullptr;

    QStatusBar *statusBar;
    QMenuBar *menuBar;

    QAction *actionNewWorkspace, *actionOpenWorkspace,
            *actionNewVMEConfig, *actionOpenVMEConfig, *actionSaveVMEConfig, *actionSaveVMEConfigAs,
            *actionOpenListfile, *actionCloseListfile,
            *actionQuit,
            *actionShowMainWindow, *actionShowAnalysis, *actionShowLog, *actionShowListfileBrowser,

            *actionToolVMEDebug, *actionToolImportHisto1D, *actionToolVMUSBFirmwareUpdate,
            *actionToolTemplateInfo, *actionToolSIS3153Debug,

            *actionHelpVMEScript, *actionHelpAbout, *actionHelpAboutQt, *actionHelpUpdateCheck
            ;

    QMenu *menuFile, *menuWindow, *menuTools, *menuHelp;
};

MVMEMainWindow::MVMEMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_d(new MVMEWindowPrivate)
{
    setObjectName(QSL("mvme"));
    setWindowTitle(QSL("mvme"));

    m_d->m_context = new MVMEContext(this, this);
    m_d->centralWidget          = new QWidget(this);
    m_d->centralLayout          = new QVBoxLayout(m_d->centralWidget);
    m_d->statusBar              = new QStatusBar(this);
    m_d->statusBar->setSizeGripEnabled(false);
    m_d->menuBar                = new QMenuBar(this);
    m_d->m_geometrySaver        = new WidgetGeometrySaver(this);
    m_d->m_networkAccessManager = new QNetworkAccessManager(this);

    setCentralWidget(m_d->centralWidget);
    setStatusBar(m_d->statusBar);
    setMenuBar(m_d->menuBar);

    //
    // create actions
    //

    m_d->actionNewWorkspace     = new QAction(QSL("New Workspace"), this);
    m_d->actionNewWorkspace->setObjectName(QSL("actionNewWorkspace"));
    m_d->actionOpenWorkspace    = new QAction(QSL("Open Workspace"), this);
    m_d->actionOpenWorkspace->setObjectName(QSL("actionOpenWorkspac"));

    m_d->actionNewVMEConfig     = new QAction(QIcon(QSL(":/document-new.png")), QSL("New VME Config"), this);
    m_d->actionNewVMEConfig->setToolTip(QSL("New VME Config"));
    m_d->actionNewVMEConfig->setIconText(QSL("New"));
    m_d->actionNewVMEConfig->setObjectName(QSL("actionNewVMEConfig"));

    m_d->actionOpenVMEConfig    = new QAction(QIcon(QSL(":/document-open.png")), QSL("Open VME Config"), this);
    m_d->actionOpenVMEConfig->setObjectName(QSL("actionOpenVMEConfig"));
    m_d->actionOpenVMEConfig->setToolTip(QSL("Open VME Config"));
    m_d->actionOpenVMEConfig->setIconText(QSL("Open"));

    m_d->actionSaveVMEConfig    = new QAction(QIcon(QSL(":/document-save.png")), QSL("Save VME Config"), this);
    m_d->actionSaveVMEConfig->setObjectName(QSL("actionSaveVMEConfig"));
    m_d->actionSaveVMEConfig->setToolTip(QSL("Save VME Config"));
    m_d->actionSaveVMEConfig->setIconText(QSL("Save"));

    m_d->actionSaveVMEConfigAs  = new QAction(QIcon(QSL(":/document-save-as.png")), QSL("Save VME Config As"), this);
    m_d->actionSaveVMEConfigAs->setObjectName(QSL("actionSaveVMEConfigAs"));
    m_d->actionSaveVMEConfigAs->setToolTip(QSL("Save VME Config As"));
    m_d->actionSaveVMEConfigAs->setIconText(QSL("Save As"));

    m_d->actionOpenListfile     = new QAction(QSL("Open Listfile"), this);
    m_d->actionCloseListfile    = new QAction(QSL("Close Listfile"), this);

    m_d->actionQuit             = new QAction(QSL("&Quit"), this);
    m_d->actionQuit->setShortcut(QSL("Ctrl+Q"));
    m_d->actionQuit->setShortcutContext(Qt::ApplicationShortcut);

    m_d->actionShowMainWindow   = new QAction(QSL("Main Window"), this);
    m_d->actionShowMainWindow->setShortcut(QSL("Ctrl+1"));
    m_d->actionShowMainWindow->setShortcutContext(Qt::ApplicationShortcut);

    m_d->actionShowAnalysis     = new QAction(QSL("Analysis"), this);
    m_d->actionShowAnalysis->setShortcut(QSL("Ctrl+2"));
    m_d->actionShowAnalysis->setShortcutContext(Qt::ApplicationShortcut);

    m_d->actionShowLog          = new QAction(QSL("Log Window"), this);
    m_d->actionShowLog->setShortcut(QSL("Ctrl+3"));
    m_d->actionShowLog->setShortcutContext(Qt::ApplicationShortcut);

    m_d->actionShowListfileBrowser = new QAction(QSL("Listfile Browser"), this);
    m_d->actionShowListfileBrowser->setShortcut(QSL("Ctrl+4"));
    m_d->actionShowListfileBrowser->setShortcutContext(Qt::ApplicationShortcut);

    m_d->actionToolVMEDebug             = new QAction(QSL("VME Debug"), this);
    m_d->actionToolImportHisto1D        = new QAction(QSL("Import Histo1D"), this);
    m_d->actionToolVMUSBFirmwareUpdate  = new QAction(QSL("VM-USB Firmware Update"), this);
    m_d->actionToolTemplateInfo         = new QAction(QSL("Template System Info"), this);
    m_d->actionToolSIS3153Debug         = new QAction(QSL("SIS3153 Debug Widget"), this);

    m_d->actionHelpVMEScript   = new QAction(QIcon(QSL(":/help.png")), QSL("&VME Script Reference"), this);
    m_d->actionHelpVMEScript->setObjectName(QSL("actionVMEScriptRef"));
    m_d->actionHelpVMEScript->setIconText(QSL("Script Help"));
    m_d->actionHelpAbout       = new QAction(QIcon(QSL("window_icon.png")), QSL("&About mvme"), this);
    m_d->actionHelpAboutQt     = new QAction(QSL("About &Qt"), this);
    m_d->actionHelpUpdateCheck = new QAction(QSL("Check for updates"), this);
    m_d->actionHelpUpdateCheck->setVisible(false); // TODO: enable update check at some point

    //
    // connect actions
    //

    connect(m_d->actionNewWorkspace,            &QAction::triggered, this, &MVMEMainWindow::onActionNewWorkspace_triggered);
    connect(m_d->actionOpenWorkspace,           &QAction::triggered, this, &MVMEMainWindow::onActionOpenWorkspace_triggered);
    connect(m_d->actionNewVMEConfig,            &QAction::triggered, this, &MVMEMainWindow::onActionNewVMEConfig_triggered);
    connect(m_d->actionOpenVMEConfig,           &QAction::triggered, this, &MVMEMainWindow::onActionOpenVMEConfig_triggered);
    connect(m_d->actionSaveVMEConfig,           &QAction::triggered, this, &MVMEMainWindow::onActionSaveVMEConfig_triggered);
    connect(m_d->actionSaveVMEConfigAs,         &QAction::triggered, this, &MVMEMainWindow::onActionSaveVMEConfigAs_triggered);
    connect(m_d->actionOpenListfile,            &QAction::triggered, this, &MVMEMainWindow::onActionOpenListfile_triggered);
    connect(m_d->actionCloseListfile,           &QAction::triggered, this, &MVMEMainWindow::onActionCloseListfile_triggered);
    connect(m_d->actionQuit,                    &QAction::triggered, this, &MVMEMainWindow::close);

    connect(m_d->actionShowMainWindow,          &QAction::triggered, this, &MVMEMainWindow::onActionMainWindow_triggered);
    connect(m_d->actionShowAnalysis,            &QAction::triggered, this, &MVMEMainWindow::onActionAnalysis_UI_triggered);
    connect(m_d->actionShowLog,                 &QAction::triggered, this, &MVMEMainWindow::onActionLog_Window_triggered);
    connect(m_d->actionShowListfileBrowser,     &QAction::triggered, this, &MVMEMainWindow::onActionListfileBrowser_triggered);

    connect(m_d->actionToolVMEDebug,            &QAction::triggered, this, &MVMEMainWindow::onActionVME_Debug_triggered);
    connect(m_d->actionToolImportHisto1D,       &QAction::triggered, this, &MVMEMainWindow::onActionImport_Histo1D_triggered);
    connect(m_d->actionToolVMUSBFirmwareUpdate, &QAction::triggered, this, &MVMEMainWindow::onActionVMUSB_Firmware_Update_triggered);
    connect(m_d->actionToolTemplateInfo,        &QAction::triggered, this, &MVMEMainWindow::onActionTemplate_Info_triggered);
    connect(m_d->actionToolSIS3153Debug,        &QAction::triggered, this, [this]() {
        auto widget = new SIS3153DebugWidget(m_d->m_context);
        widget->setAttribute(Qt::WA_DeleteOnClose);
        add_widget_close_action(widget);
        m_d->m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/SIS3153DebugWidget"));
        widget->show();
    });

    connect(m_d->actionHelpVMEScript,           &QAction::triggered, this, &MVMEMainWindow::onActionVMEScriptRef_triggered);
    connect(m_d->actionHelpAbout,               &QAction::triggered, this, &MVMEMainWindow::displayAbout);
    connect(m_d->actionHelpAboutQt,             &QAction::triggered, this, &MVMEMainWindow::displayAboutQt);
    connect(m_d->actionHelpUpdateCheck,         &QAction::triggered, this, &MVMEMainWindow::onActionCheck_for_updates_triggered);


    //
    // populate menus
    //

    m_d->menuFile   = new QMenu(QSL("&File"), this);
    m_d->menuWindow = new QMenu(QSL("&Window"), this);
    m_d->menuTools  = new QMenu(QSL("&Tools"), this);
    m_d->menuHelp   = new QMenu(QSL("&Help"), this);

    m_d->menuFile->addAction(m_d->actionNewWorkspace);
    m_d->menuFile->addAction(m_d->actionOpenWorkspace);
    m_d->menuFile->addSeparator();
    m_d->menuFile->addAction(m_d->actionNewVMEConfig);
    m_d->menuFile->addAction(m_d->actionOpenVMEConfig);
    m_d->menuFile->addAction(m_d->actionSaveVMEConfig);
    m_d->menuFile->addAction(m_d->actionSaveVMEConfigAs);
    m_d->menuFile->addSeparator();
    m_d->menuFile->addAction(m_d->actionOpenListfile);
    m_d->menuFile->addAction(m_d->actionCloseListfile);
    m_d->menuFile->addSeparator();
    m_d->menuFile->addAction(m_d->actionQuit);

    m_d->menuWindow->addAction(m_d->actionShowMainWindow);
    m_d->menuWindow->addAction(m_d->actionShowAnalysis);
    m_d->menuWindow->addAction(m_d->actionShowLog);
    m_d->menuWindow->addAction(m_d->actionShowListfileBrowser);

    m_d->menuTools->addAction(m_d->actionToolVMEDebug);
    m_d->menuTools->addAction(m_d->actionToolImportHisto1D);
    m_d->menuTools->addAction(m_d->actionToolVMUSBFirmwareUpdate);
    m_d->menuTools->addAction(m_d->actionToolTemplateInfo);
    m_d->menuTools->addAction(m_d->actionToolSIS3153Debug);
    m_d->menuTools->addAction(m_d->actionToolVMEDebug);

    m_d->menuHelp->addAction(m_d->actionHelpVMEScript);
    m_d->menuHelp->addSeparator();
    m_d->menuHelp->addAction(m_d->actionHelpAbout);
    m_d->menuHelp->addAction(m_d->actionHelpAboutQt);
    m_d->menuHelp->addAction(m_d->actionHelpUpdateCheck);

    m_d->menuBar->addMenu(m_d->menuFile);
    m_d->menuBar->addMenu(m_d->menuWindow);
    m_d->menuBar->addMenu(m_d->menuTools);
    m_d->menuBar->addMenu(m_d->menuHelp);

    connect(m_d->m_context, &MVMEContext::daqConfigFileNameChanged, this, &MVMEMainWindow::updateWindowTitle);
    connect(m_d->m_context, &MVMEContext::modeChanged, this, &MVMEMainWindow::updateWindowTitle);
    connect(m_d->m_context, &MVMEContext::daqConfigChanged, this, &MVMEMainWindow::onConfigChanged);
    connect(m_d->m_context, &MVMEContext::objectAboutToBeRemoved, this, &MVMEMainWindow::onObjectAboutToBeRemoved);
    connect(m_d->m_context, &MVMEContext::daqAboutToStart, this, &MVMEMainWindow::onDAQAboutToStart);
    connect(m_d->m_context, &MVMEContext::daqStateChanged, this, &MVMEMainWindow::onDAQStateChanged);
    connect(m_d->m_context, &MVMEContext::sigLogMessage, this, &MVMEMainWindow::appendToLog);
    connect(m_d->m_context, &MVMEContext::daqStateChanged, this, &MVMEMainWindow::updateActions);
    connect(m_d->m_context, &MVMEContext::eventProcessorStateChanged, this, &MVMEMainWindow::updateActions);
    connect(m_d->m_context, &MVMEContext::modeChanged, this, &MVMEMainWindow::updateActions);
    connect(m_d->m_context, &MVMEContext::daqConfigChanged, this, &MVMEMainWindow::updateActions);

    //
    // central widget consisting of DAQControlWidget, DAQConfigTreeWidget and DAQStatsWidget
    //
    {
        m_d->m_daqControlWidget = new DAQControlWidget(m_d->m_context);
        m_d->m_vmeConfigTreeWidget = new VMEConfigTreeWidget(m_d->m_context);
        m_d->m_daqStatsWidget = new DAQStatsWidget(m_d->m_context);

        auto centralLayout = m_d->centralLayout;

        centralLayout->setContentsMargins(6, 6, 6, 0); // l, t, r, b
        centralLayout->addWidget(m_d->m_daqControlWidget);
        centralLayout->addWidget(m_d->m_vmeConfigTreeWidget);
        centralLayout->addWidget(m_d->m_daqStatsWidget);

        centralLayout->setStretch(1, 1);

        connect(m_d->m_vmeConfigTreeWidget, &VMEConfigTreeWidget::showDiagnostics,
                this, &MVMEMainWindow::onShowDiagnostics);
    }

    updateWindowTitle();

    // Code to run on entering the event loop for the first time.
    QTimer::singleShot(0, [this] () {
        updateActions();

        // Create and open log and analysis windows.
        onActionLog_Window_triggered();
        onActionAnalysis_UI_triggered();
        //onActionListfileBrowser_triggered();
        // Focus the main window
        this->raise();
    });
}

MVMEMainWindow::~MVMEMainWindow()
{
    // To avoid a crash on exit if replay is running
    disconnect(m_d->m_context, &MVMEContext::daqStateChanged, this, &MVMEMainWindow::onDAQStateChanged);

    auto workspaceDir = m_d->m_context->getWorkspaceDirectory();

    if (!workspaceDir.isEmpty())
    {
        QSettings settings;
        settings.setValue("LastWorkspaceDirectory", workspaceDir);
    }

    qDebug() << __PRETTY_FUNCTION__ << "MVMEMainWindow instance being destroyed";

    delete m_d;
}

MVMEContext *MVMEMainWindow::getContext()
{
    return m_d->m_context;
}

void MVMEMainWindow::loadConfig(const QString &fileName)
{
    m_d->m_context->loadVMEConfig(fileName);
}

void MVMEMainWindow::onActionNewWorkspace_triggered()
{
    auto startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    auto dirName  = QFileDialog::getExistingDirectory(this, QSL("Create new workspace directory"), startDir);

    if (dirName.isEmpty())
    {
        // Dialog was canceled
        return;
    }

    try
    {
        m_d->m_context->newWorkspace(dirName);
    } catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Workspace Error"), QString("Error creating workspace: %1").arg(e));
    }
}

void MVMEMainWindow::onActionOpenWorkspace_triggered()
{
   /* Use the parent directory of last opened workspace as the start directory
    * for browsing. */
    auto startDir = QSettings().value("LastWorkspaceDirectory").toString();

    if (!startDir.isEmpty())
    {
        QDir dir(startDir);
        dir.cdUp();
        startDir = dir.absolutePath();
    }
    else
    {
        startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    auto dirName  = QFileDialog::getExistingDirectory(this, QSL("Select workspace"), startDir);

    if (dirName.isEmpty())
        return;

    try
    {
        m_d->m_context->openWorkspace(dirName);
    } catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Workspace Error"), QString("Error opening workspace: %1").arg(e));
    }
}

void MVMEMainWindow::displayAbout()
{
    auto dialog = new QDialog(this);
    dialog->setWindowTitle(QSL("About mvme"));

    auto tb_license = new QTextBrowser(dialog);
    tb_license->setWindowFlags(Qt::Window);
    tb_license->setWindowTitle(QSL("mvme license"));

    {
        QFile licenseFile(":/gpl-notice.txt");
        licenseFile.open(QIODevice::ReadOnly);
        tb_license->setText(licenseFile.readAll());
    }

    auto layout = new QVBoxLayout(dialog);

    {
        auto label = new QLabel;
        label->setPixmap(QPixmap(":/mesytec-logo.png").
                              scaledToWidth(300, Qt::SmoothTransformation));
        layout->addWidget(label);
    }

    auto bitness = get_bitness_string();

    QString versionString = QString("Version %1").arg(GIT_VERSION);
    if (!bitness.isEmpty())
    {
        versionString += QString(" (%1)").arg(bitness);
    }

    layout->addWidget(new QLabel(QSL("mvme - VME Data Acquisition")));
    layout->addWidget(new QLabel(versionString));
    layout->addWidget(new QLabel(QSL("© 2015-2017 mesytec GmbH & Co. KG")));
    layout->addWidget(new QLabel(QSL("Authors: F. Lüke, R. Schneider")));

    {
        QString text(QSL("<a href=\"mailto:info@mesytec.com\">info@mesytec.com</a> - <a href=\"http://www.mesytec.com\">www.mesytec.com</a>"));
        auto label = new QLabel(text);
        label->setOpenExternalLinks(true);
        layout->addWidget(label);
    }

    layout->addSpacing(20);

    auto buttonLayout = new QHBoxLayout;

    // license
    {
        auto button = new QPushButton(QSL("&License"));
        connect(button, &QPushButton::clicked, this, [this, tb_license]() {
            auto sz = tb_license->size();
            sz = sz.expandedTo(QSize(500, 300));
            tb_license->resize(sz);
            tb_license->show();
            tb_license->raise();
        });

        buttonLayout->addWidget(button);
    }

    // build info
    {
        QStringList build_infos;
        build_infos << versionString;
        build_infos << QSL("Build Type: ") + BUILD_TYPE;
        build_infos << QSL("Build Flags:") + BUILD_CXX_FLAGS;

        auto tb_info = new QTextBrowser(dialog);
        tb_info->setWindowFlags(Qt::Window);
        tb_info->setWindowTitle(QSL("mvme build info"));
        tb_info->setText(build_infos.join('\n'));

        auto button = new QPushButton(QSL("&Info"));
        connect(button, &QPushButton::clicked, this, [this, tb_info]() {
            auto sz = tb_info->size();
            sz = sz.expandedTo(QSize(500, 300));
            tb_info->resize(sz);
            tb_info->show();
            tb_info->raise();
        });

        buttonLayout->addWidget(button);
    }

    // close
    {
        auto button = new QPushButton(QSL("&Close"));
        connect(button, &QPushButton::clicked, dialog, &QDialog::close);
        button->setAutoDefault(true);
        button->setDefault(true);
        buttonLayout->addWidget(button);
    }

    layout->addLayout(buttonLayout);

    for (int i=0; i<layout->count(); ++i)
    {
        auto item = layout->itemAt(i);
        item->setAlignment(Qt::AlignHCenter);

        auto label = qobject_cast<QLabel *>(item->widget());
        if (label)
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    }

    dialog->exec();
}

void MVMEMainWindow::displayAboutQt()
{
    QMessageBox::aboutQt(this, QSL("About Qt"));
}

static const QString DefaultAnalysisFileFilter = QSL("Config Files (*.analysis);; All Files (*.*)");

void MVMEMainWindow::closeEvent(QCloseEvent *event)
{
    // Refuse to exit if DAQ is running.
    if (m_d->m_context->getDAQState() != DAQState::Idle && m_d->m_context->getMode() == GlobalMode::DAQ)
    {
        QMessageBox msgBox(QMessageBox::Warning, QSL("DAQ is running"),
                           QSL("Data acquisition is currently active. Ignoring request to exit."),
                           QMessageBox::Ok);
        msgBox.exec();
        event->ignore();
        return;
    }

    // Handle modified DAQConfig
    if (m_d->m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save DAQ configuration?"),
                           QSL("The current DAQ configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!onActionSaveVMEConfig_triggered())
            {
                event->ignore();
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
    }

    // Handle modified AnalysisConfig
    auto analysis = m_d->m_context->getAnalysis();
    if (analysis->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis config?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            auto result = saveAnalysisConfig(m_d->m_context->getAnalysis(),
                                             m_d->m_context->getAnalysisConfigFileName(),
                                             m_d->m_context->getWorkspaceDirectory(),
                                             DefaultAnalysisFileFilter,
                                             m_d->m_context);
            if (!result.first)
            {
                event->ignore();
                return;
            }
            m_d->m_context->setAnalysisConfigFileName(result.second);
        }
        else if (result == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
    }

    /* Try to close all top level windows except our own window. This will
     * trigger any reimplementations of closeEvent() and thus give widgets a
     * chance to ask the user about how to handle pending modifications. If the
     * QCloseEvent is ignored by the widget the QWindow::close() call will
     * return false. In this case we keep this widget open and ignore our
     * QCloseEvent aswell.  */
    bool allWindowsClosed = true;

    for (auto window: QGuiApplication::topLevelWindows())
    {
        if (window != this->windowHandle())
        {
            if (!window->close())
            {
                qDebug() << __PRETTY_FUNCTION__ << "window" << window << "refused to close";
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

    m_quitting = true;

    QMainWindow::closeEvent(event);
    auto app = qobject_cast<QApplication *>(qApp);
    if (app)
    {
        app->closeAllWindows();
    }
}

void MVMEMainWindow::restoreSettings()
{
    qDebug("restoreSettings");
    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());
}

void MVMEMainWindow::addObjectWidget(QWidget *widget, QObject *object, const QString &stateKey)
{
    connect(widget, &QObject::destroyed, this, [this, object, widget] (QObject *) {
        m_d->m_objectWindows[object].removeOne(widget);
    });

    widget->setAttribute(Qt::WA_DeleteOnClose);
    m_d->m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/") + stateKey);
    add_widget_close_action(widget);

    m_d->m_objectWindows[object].push_back(widget);
    widget->show();
}

bool MVMEMainWindow::hasObjectWidget(QObject *object) const
{
    return !m_d->m_objectWindows[object].isEmpty();
}

QWidget *MVMEMainWindow::getObjectWidget(QObject *object) const
{
    QWidget *result = nullptr;
    const auto &l(m_d->m_objectWindows[object]);

    if (!l.isEmpty())
    {
        result = l.last();
    }

    return result;
}

QList<QWidget *> MVMEMainWindow::getObjectWidgets(QObject *object) const
{
    return m_d->m_objectWindows[object];
}

void MVMEMainWindow::activateObjectWidget(QObject *object)
{
    if (auto widget = getObjectWidget(object))
    {
        show_and_activate(widget);
    }
}

void MVMEMainWindow::addWidget(QWidget *widget, const QString &stateKey)
{
    widget->setAttribute(Qt::WA_DeleteOnClose);
    if (!stateKey.isEmpty())
        m_d->m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/") + stateKey);
    add_widget_close_action(widget);
    widget->show();
}

void MVMEMainWindow::onActionNewVMEConfig_triggered()
{
    if (m_d->m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save configuration?",
                           "The current configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!onActionSaveVMEConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    // copy the previous controller settings into the new VMEConfig
    auto vmeConfig = m_d->m_context->getVMEConfig();
    auto ctrlType = vmeConfig->getControllerType();
    auto ctrlSettings = vmeConfig->getControllerSettings();

    vmeConfig = new VMEConfig;
    vmeConfig->setVMEController(ctrlType, ctrlSettings);

    m_d->m_context->setVMEConfig(vmeConfig);
    m_d->m_context->setConfigFileName(QString());
    m_d->m_context->setMode(GlobalMode::DAQ);
}

// Note: .mvmecfg was the old extension when vme and analysis config where not separated yet.
static const QString VMEConfigFileFilter = QSL("Config Files (*.vme *.mvmecfg);; All Files (*.*)");

void MVMEMainWindow::onActionOpenVMEConfig_triggered()
{
    if (m_d->m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save VME configuration?",
                           "The current VME configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!onActionSaveVMEConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    auto path = m_d->m_context->getWorkspaceDirectory();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load MVME Config", path, VMEConfigFileFilter);

    if (fileName.isEmpty())
        return;

    loadConfig(fileName);
}

bool MVMEMainWindow::onActionSaveVMEConfig_triggered()
{
    if (m_d->m_context->getConfigFileName().isEmpty())
    {
        return onActionSaveVMEConfigAs_triggered();
    }

    QString fileName = m_d->m_context->getConfigFileName();
    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    QJsonObject daqConfigJson;
    m_d->m_context->getVMEConfig()->write(daqConfigJson);
    QJsonObject configObject;
    configObject["DAQConfig"] = daqConfigJson;
    QJsonDocument doc(configObject);

    if (outFile.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    auto config = m_d->m_context->getConfig();
    config->setModified(false);
    auto configObjects = config->findChildren<ConfigObject *>();
    for (auto obj: configObjects)
    {
        obj->setModified(false);
    }

    updateWindowTitle();
    return true;
}

bool MVMEMainWindow::onActionSaveVMEConfigAs_triggered()
{
    QString path = QFileInfo(m_d->m_context->getConfigFileName()).absolutePath();

    if (path.isEmpty())
        path = m_d->m_context->getWorkspaceDirectory();

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getSaveFileName(this, "Save Config As", path, VMEConfigFileFilter);

    if (fileName.isEmpty())
        return false;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += QSL(".vme");
    }

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return false;
    }

    QJsonObject daqConfigJson;
    m_d->m_context->getVMEConfig()->write(daqConfigJson);
    QJsonObject configObject;
    configObject["DAQConfig"] = daqConfigJson;
    QJsonDocument doc(configObject);

    if (outFile.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    m_d->m_context->setConfigFileName(fileName);
    m_d->m_context->getConfig()->setModified(false);
    updateWindowTitle();
    return true;
}

void MVMEMainWindow::onActionOpenListfile_triggered()
{
    if (m_d->m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save configuration?",
                           "The current VME configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!onActionSaveVMEConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    QString path = m_d->m_context->getListFileOutputInfo().fullDirectory;

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load Listfile",
                                                    path,
                                                    "MVME Listfiles (*.mvmelst *.zip);; All Files (*.*)");
    if (fileName.isEmpty())
    {
        return;
    }

    try
    {
        auto openResult = open_listfile(m_d->m_context, fileName, OpenListfileFlags::LoadAnalysis);

        appendToLogNoDebugOut(QSL(">>>>> Begin listfile log"));
        appendToLogNoDebugOut(openResult.messages);
        appendToLogNoDebugOut(QSL("<<<<< End listfile log"));
    }
    catch (const QString &err)
    {
        QMessageBox::critical(0, "Error", err);
    }

    updateWindowTitle();
}

void MVMEMainWindow::onActionCloseListfile_triggered()
{
    m_d->m_context->closeReplayFile();
}

void MVMEMainWindow::onActionMainWindow_triggered()
{
    show_and_activate(this);
}

void MVMEMainWindow::onActionAnalysis_UI_triggered()
{
    auto analysisUi = m_d->m_context->getAnalysisUi();

    if (!analysisUi)
    {
        analysisUi = new analysis::AnalysisWidget(m_d->m_context);
        m_d->m_context->setAnalysisUi(analysisUi);

        connect(analysisUi, &QObject::destroyed, this, [this] (QObject *) {
            this->m_d->m_context->setAnalysisUi(nullptr);
        });

        add_widget_close_action(analysisUi);
        m_d->m_geometrySaver->addAndRestore(analysisUi, QSL("WindowGeometries/AnalysisUI"));
        analysisUi->setAttribute(Qt::WA_DeleteOnClose);
    }

    show_and_activate(analysisUi);
}

void MVMEMainWindow::onActionVME_Debug_triggered()
{
    if (!m_d->m_vmeDebugWidget)
    {
        m_d->m_vmeDebugWidget = new VMEDebugWidget(m_d->m_context);
        m_d->m_vmeDebugWidget->setAttribute(Qt::WA_DeleteOnClose);

        connect(m_d->m_vmeDebugWidget, &QObject::destroyed, this, [this] (QObject *) {
            this->m_d->m_vmeDebugWidget = nullptr;
        });

        add_widget_close_action(m_d->m_vmeDebugWidget);
        m_d->m_geometrySaver->addAndRestore(m_d->m_vmeDebugWidget, QSL("WindowGeometries/VMEDebug"));
    }

    show_and_activate(m_d->m_vmeDebugWidget);
}

static const size_t LogViewMaximumBlockCount = Megabytes(1);

void MVMEMainWindow::onActionLog_Window_triggered()
{
    if (!m_d->m_logView)
    {
        m_d->m_logView = new QPlainTextEdit;
        m_d->m_logView->setAttribute(Qt::WA_DeleteOnClose);
        m_d->m_logView->setReadOnly(true);
        m_d->m_logView->setWindowTitle("Log View");
        QFont font("MonoSpace");
        font.setStyleHint(QFont::Monospace);
        m_d->m_logView->setFont(font);
        m_d->m_logView->setTabChangesFocus(true);
        m_d->m_logView->document()->setMaximumBlockCount(LogViewMaximumBlockCount);
        m_d->m_logView->setContextMenuPolicy(Qt::CustomContextMenu);
        m_d->m_logView->setStyleSheet("background-color: rgb(225, 225, 225);");
        add_widget_close_action(m_d->m_logView);

        connect(m_d->m_logView, &QWidget::customContextMenuRequested, this, [=](const QPoint &pos) {
            auto menu = m_d->m_logView->createStandardContextMenu(pos);
            auto action = menu->addAction("Clear");
            connect(action, &QAction::triggered, m_d->m_logView, &QPlainTextEdit::clear);
            menu->exec(m_d->m_logView->mapToGlobal(pos));
            menu->deleteLater();
        });
        connect(m_d->m_logView, &QObject::destroyed, this, [this] (QObject *) {
            this->m_d->m_logView = nullptr;
        });

        m_d->m_geometrySaver->addAndRestore(m_d->m_logView, QSL("WindowGeometries/LogView"));
    }

    show_and_activate(m_d->m_logView);
}

void MVMEMainWindow::onActionListfileBrowser_triggered()
{
    if (!m_d->m_listfileBrowser)
    {
        m_d->m_listfileBrowser = new ListfileBrowser(m_d->m_context);
        m_d->m_listfileBrowser->setAttribute(Qt::WA_DeleteOnClose);
        add_widget_close_action(m_d->m_listfileBrowser);

        connect(m_d->m_listfileBrowser, &QObject::destroyed, this, [this] (QObject *) {
            this->m_d->m_listfileBrowser = nullptr;
        });

        m_d->m_geometrySaver->addAndRestore(m_d->m_listfileBrowser, QSL("WindowGeometries/ListfileBrowser"));
    }

    show_and_activate(m_d->m_listfileBrowser);
}

void MVMEMainWindow::onActionVMUSB_Firmware_Update_triggered()
{
    vmusb_gui_load_firmware(m_d->m_context);
}

void MVMEMainWindow::onActionTemplate_Info_triggered()
{
    QString buffer;
    QTextStream logStream(&buffer);

    logStream << "Reading templates..." << endl;
    MVMETemplates templates = read_templates([&logStream](const QString &msg) {
        logStream << msg << endl;
    });

    logStream << endl << templates;

    auto textEdit = new QPlainTextEdit;
    textEdit->setAttribute(Qt::WA_DeleteOnClose);
    textEdit->setReadOnly(true);
    textEdit->setWindowTitle("VME/Analysis Template System Info");
    QFont font("MonoSpace");
    font.setStyleHint(QFont::Monospace);
    textEdit->setFont(font);
    textEdit->setTabChangesFocus(true);
    textEdit->resize(600, 500);
    textEdit->setPlainText(buffer);
    add_widget_close_action(textEdit);
    m_d->m_geometrySaver->addAndRestore(textEdit, QSL("WindowGeometries/VATSInfo"));
    textEdit->show();
}

void MVMEMainWindow::onObjectAboutToBeRemoved(QObject *object)
{
    auto &windowList = m_d->m_objectWindows[object];

    qDebug() << __PRETTY_FUNCTION__ << object << windowList;

    for (auto subwin: windowList)
        subwin->close();

    m_d->m_objectWindows.remove(object);
}

void MVMEMainWindow::appendToLogNoDebugOut(const QString &str)
{
    if (m_d->m_logView)
    {
        m_d->m_logView->appendPlainText(str);
        auto bar = m_d->m_logView->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void MVMEMainWindow::appendToLog(const QString &str)
{
    //qDebug().noquote() << __PRETTY_FUNCTION__ << str;

    if (m_d->m_logView)
    {
        m_d->m_logView->appendPlainText(str);
        auto bar = m_d->m_logView->verticalScrollBar();
        bar->setValue(bar->maximum());
    }
}

void MVMEMainWindow::updateWindowTitle()
{
    QDir wsDir(m_d->m_context->getWorkspaceDirectory());
    QString workspaceDir(wsDir.dirName());

    QString title;
    switch (m_d->m_context->getMode())
    {
        case GlobalMode::DAQ:
            {
                title = QString("%1 - [DAQ mode] - mvme")
                    .arg(workspaceDir);
            } break;

        case GlobalMode::ListFile:
            {
                auto listFile = m_d->m_context->getReplayFile();
                QString fileName(QSL("<no listfile>"));
                if (listFile)
                {
                    QString filePath = listFile->getFileName();
                    fileName =  QFileInfo(filePath).fileName();
                }

                title = QString("%1 - %2 - [ListFile mode] - mvme")
                    .arg(workspaceDir)
                    .arg(fileName);
            } break;

        case GlobalMode::NotSet:
            break;
    }

    if (m_d->m_context->getMode() == GlobalMode::DAQ && m_d->m_context->isWorkspaceModified())
    {
        title += " *";
    }

    setWindowTitle(title);
}

void MVMEMainWindow::onConfigChanged(VMEConfig *config)
{
    connect(config, &VMEConfig::modifiedChanged, this, &MVMEMainWindow::updateWindowTitle);
    updateWindowTitle();
}

void MVMEMainWindow::clearLog()
{
    if (m_d->m_logView)
    {
        m_d->m_logView->clear();
    }
}

void MVMEMainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}

void MVMEMainWindow::onDAQAboutToStart(quint32 nCycles)
{
    QList<VMEScriptEditor *> scriptEditors;

    for (auto widgetList: m_d->m_objectWindows.values())
    {
        for (auto widget: widgetList)
        {
            if (auto scriptEditor = qobject_cast<VMEScriptEditor *>(widget))
            {
                if (scriptEditor->isModified())
                {
                    scriptEditors.push_back(scriptEditor);
                }
            }
        }
    }

    if (!scriptEditors.isEmpty())
    {
        QMessageBox box(QMessageBox::Question, QSL("Pending script modifications"),
                        QSL("Some VME scripts have been modified.\nDo you want to use those modifications for the current DAQ run?"),
                        QMessageBox::Yes | QMessageBox::No);

        int result = box.exec();

        if (result == QMessageBox::Yes)
        {
            for (auto scriptEditor: scriptEditors)
            {
                scriptEditor->applyChanges();
            }
        }
    }
}

void MVMEMainWindow::onDAQStateChanged(const DAQState &)
{
    auto globalMode = m_d->m_context->getMode();
    auto daqState = m_d->m_context->getDAQState();

    {
        bool enable = true;

        if (globalMode == GlobalMode::DAQ && daqState != DAQState::Idle)
        {
            enable = false;
        }

        m_d->actionOpenListfile->setEnabled(enable);
    }
}

void MVMEMainWindow::onShowDiagnostics(ModuleConfig *moduleConfig)
{
    if (m_d->m_context->getEventProcessor()->getDiagnostics())
        return;

    auto diag   = new MesytecDiagnostics;
    diag->setEventAndModuleIndices(m_d->m_context->getVMEConfig()->getEventAndModuleIndices(moduleConfig));
    auto eventProcessor = m_d->m_context->getEventProcessor();
    eventProcessor->setDiagnostics(diag);

    auto widget = new MesytecDiagnosticsWidget(diag);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    add_widget_close_action(widget);
    m_d->m_geometrySaver->addAndRestore(widget, QSL("WindowGeometries/MesytecDiagnostics"));

    connect(widget, &MVMEWidget::aboutToClose, this, [this]() {
        qDebug() << __PRETTY_FUNCTION__ << "diagnostics widget about to close";
        QMetaObject::invokeMethod(m_d->m_context->getEventProcessor(), "removeDiagnostics", Qt::QueuedConnection);
    });

    connect(m_d->m_context, &MVMEContext::daqStateChanged, widget, [this, widget] (const DAQState &state) {
        if (state == DAQState::Running)
        {
            widget->clearResultsDisplay();
        }

    });

    widget->show();
    widget->raise();
}

void MVMEMainWindow::onActionImport_Histo1D_triggered()
{
    QSettings settings;
    QString path = settings.value(QSL("Files/LastHistogramExportDirectory")).toString();

    if (path.isEmpty())
    {
        path = m_d->m_context->getWorkspaceDirectory();
    }

    QString filename = QFileDialog::getOpenFileName(
        this, QSL("Import Histogram"),
        path,
        QSL("Histogram files (*.histo1d);; All Files (*)"));

    if (filename.isEmpty())
        return;

    QFile inFile(filename);

    if (inFile.open(QIODevice::ReadOnly))
    {
        QTextStream inStream(&inFile);

        auto histo = readHisto1D(inStream);

        if (histo)
        {
            auto widget = new Histo1DWidget(histo);
            widget->setAttribute(Qt::WA_DeleteOnClose);
            histo->setParent(widget);

            path = QFileInfo(filename).dir().path();

            if (path != m_d->m_context->getWorkspaceDirectory())
            {
                settings.setValue(QSL("Files/LastHistogramExportDirectory"), path);
            }
            else
            {
                settings.remove(QSL("Files/LastHistogramExportDirectory"));
            }

            addWidget(widget);
        }
    }
}

void MVMEMainWindow::onActionVMEScriptRef_triggered()
{
    auto widgets = QApplication::topLevelWidgets();
    auto it = std::find_if(widgets.begin(), widgets.end(), [](const QWidget *widget) {
        return widget->objectName() == QSL("VMEScriptReference");
    });

    if (it != widgets.end())
    {
        auto widget = *it;
        show_and_activate(widget);
    }
    else
    {
        auto widget = make_vme_script_ref_widget();
        addWidget(widget, widget->objectName());
    }
}

static const auto UpdateCheckURL = QSL("http://mesytec.com/downloads/mvme/");
static const QByteArray UpdateCheckUserAgent = "mesytec mvme ";

static const QString get_package_platform_string()
{
#ifdef Q_OS_WIN
    return QSL("Windows");
#elif defined Q_OS_LINUX
    return QSL("Linux");
#else
    #warning "Unknown platform name."
    InvalidCodePath;
    return QString();
#endif
}

static const QString get_package_bitness_string()
{
#ifdef Q_PROCESSOR_X86_64
    return QSL("x64");
#elif defined Q_PROCESSOR_X86_32
    return QSL("x32");
#else
#warning "Unknown processor bitness."
    InvalidCodePath;
    return QString();
#endif
}

static u64 extract_package_version(const QString &filename)
{
    static const QString pattern = QSL("mvme-(?<major>[0-9]+)\\.(?<minor>[0-9]+)(\\.(?<point>[0-9]+))?(-(?<commits>[0-9]+))?");

    u64 result = 0;
    QRegularExpression re(pattern);
    auto match = re.match(filename);

    if (match.hasMatch())
    {
        u64 major   = match.captured("major").toUInt();
        u64 minor   = match.captured("minor").toUInt();
        u64 point   = match.captured("point").toUInt();
        u64 commits = match.captured("commits").toUInt();

        result = commits
            + 1000 * point
            + 1000 * 1000 * minor
            + 1000 * 1000 * 1000 * major;

        qDebug() << "filename =" << filename
            << ", major =" << major
            << ", minor =" << minor
            << ", point =" << point
            << ", commits =" << commits
            << ", result =" << result;
            ;

    }

    return result;
}

void MVMEMainWindow::onActionCheck_for_updates_triggered()
{
#if 1
    QStringList testFilenames =
    {
        "mvme-0.9.exe",
        "mvme-0.9-42.exe",
        "mvme-0.9.1.exe",
        "mvme-0.9.1-42.exe",
        "mvme-1.1.1-111-Window-x42.zip",
        "mvme-98.76.54-32-Window-x42.zip",
        "mvme-987.654.321-666-Window-x42.zip",
        "mvme-999.999.999-999-Window-x42.zip", // max version possible (except for major which could be larger)
        "mvme-9999.999.999-999-Window-x42.zip" // exceeding the max with major
    };

    for (auto name: testFilenames)
    {
        u32 version = extract_package_version(name);
        //qDebug() << __PRETTY_FUNCTION__ << "name =" << name << ", version =" << version;
    }
#endif


    QNetworkRequest request;
    request.setUrl(UpdateCheckURL);
    request.setRawHeader("User-Agent", UpdateCheckUserAgent + GIT_VERSION_TAG);

    auto reply = m_d->m_networkAccessManager->get(request);


    connect(reply, &QNetworkReply::finished, [this, reply]() {
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError)
        {
            // Both the platform and bitness strings are known at compile time.
            // A download link entry looks like this:
            // <a href="mvme-0.9-5-Windows-x32.exe">mvme-0.9-5-Windows-x32.exe</a>   2017-07-24 15:22   28M
            static const QString pattern = QString("href=\"(mvme-[0-9.-]+-%1-%2\\.exe)\"")
                .arg(get_package_platform_string())
                .arg(get_package_bitness_string())
                ;

            qDebug() << "update search pattern:" << pattern;

            QRegularExpression re(pattern);

            auto contents = QString::fromLatin1(reply->readAll());
            auto matchIter = re.globalMatch(contents);

            struct PackageInfo
            {
                QString filename;
                u32 version;
            };

            QVector<PackageInfo> packages;

            while (matchIter.hasNext())
            {
                auto match = matchIter.next();
                qDebug() << match.captured(1);
                PackageInfo info;
                info.filename = match.captured(1);
                info.version  = extract_package_version(info.filename);
                packages.push_back(info);
            }

            auto latestPackage = std::accumulate(packages.begin(), packages.end(), PackageInfo{QString(), 0},
                                               [](const auto &a, const auto &b) {
                return (a.version > b.version ? a : b);
            });

            qDebug() << __PRETTY_FUNCTION__ << "latest package version available is" << latestPackage.filename << latestPackage.version;

            // TODO: build version info from the running binary using GIT_VERSION_SHORT
        }
    });
}

bool MVMEMainWindow::createNewOrOpenExistingWorkspace()
{
    do
    {
        auto startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
        auto dirName  = QFileDialog::getExistingDirectory(this, QSL("Create a new or select an existing workspace directory"), startDir);

        if (dirName.isEmpty())
        {
            // Dialog was canceled
            return false;
        }

        QDir dir(dirName);

        bool triedToOpenExisting = true;

        try
        {
            if (dir.entryList(QDir::AllEntries | QDir::NoDot | QDir::NoDotDot).isEmpty())
            {
                triedToOpenExisting = false;
                m_d->m_context->newWorkspace(dirName);
            }
            else
            {
                triedToOpenExisting = true;
                m_d->m_context->openWorkspace(dirName);
            }
        } catch (const QString &e)
        {
            Q_ASSERT(!m_d->m_context->isWorkspaceOpen());

            QString title;
            if (triedToOpenExisting)
            {
                title = QSL("Error opening workspace");
            }
            else
            {
                title = QSL("Error creating workspace");
            }

            QMessageBox::warning(this, title, e);
        }
    } while (!m_d->m_context->isWorkspaceOpen());

    return true;
}

void MVMEMainWindow::updateActions()
{
    if (m_quitting) return;

    auto globalMode = m_d->m_context->getMode();
    auto daqState = m_d->m_context->getDAQState();
    auto eventProcState = m_d->m_context->getEventProcessorState();

    bool isDAQIdle = (daqState == DAQState::Idle);

    // Workspaces
    m_d->actionNewWorkspace->setEnabled(isDAQIdle);
    m_d->actionOpenWorkspace->setEnabled(isDAQIdle);

    // VME Config
    m_d->actionNewVMEConfig->setEnabled(isDAQIdle);
    m_d->actionOpenVMEConfig->setEnabled(isDAQIdle);

    // Listfiles
    m_d->actionOpenListfile->setEnabled(isDAQIdle);
    m_d->actionCloseListfile->setEnabled(isDAQIdle);
}
