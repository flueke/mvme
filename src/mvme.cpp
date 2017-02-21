#include "mvme.h"

#include "ui_mvme.h"
#include "vmusb.h"
#include "mvme_context.h"
#include "hist1d.h"
#include "hist2d.h"
#include "mvmedefines.h"
#include "vmusb_readout_worker.h"
#include "config_ui.h"
#include "mvme_listfile.h"
#include "daqconfig_tree.h"
#include "vme_script_editor.h"
#include "vme_debug_widget.h"
#include "histogram_tree.h"
#include "daqcontrol_widget.h"
#include "daqstats_widget.h"
#include "mesytec_diagnostics.h"
#include "mvme_event_processor.h"
#include "gui_util.h"
#include "analysis/analysis.h"
#include "histo1d_widget.h"
#include "histo2d_widget.h"
#include "analysis/analysis_ui.h"

#include <QDockWidget>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QList>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextEdit>
#include <QtGui>
#include <QToolBar>

#include <qwt_plot_curve.h>

template<typename T>
QList<T *> getSubwindowWidgetsByType(QMdiArea *mdiArea)
{
    QList<T *> ret;

    for (auto subwin: mdiArea->subWindowList())
    {
        auto w = qobject_cast<T *>(subwin->widget());
        if (w)
        {
            ret.append(w);
        }
    }

    return ret;
}

template<typename T>
QList<QMdiSubWindow *> getSubwindowsByWidgetType(QMdiArea *mdiArea)
{
    QList<QMdiSubWindow *> ret;

    for (auto subwin: mdiArea->subWindowList())
    {
        auto w = qobject_cast<T *>(subwin->widget());
        if (w)
        {
            ret.append(subwin);
        }
    }

    return ret;
}

mvme::mvme(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::mvme)
    , m_context(new MVMEContext(this, this))
    , m_logView(new QTextBrowser)
{
    qDebug() << "main thread: " << QThread::currentThread();

    setWindowIcon(QIcon(QPixmap(":/mesytec-window-icon.png")));

    connect(m_context, &MVMEContext::daqConfigFileNameChanged, this, &mvme::updateWindowTitle);
    connect(m_context, &MVMEContext::modeChanged, this, &mvme::updateWindowTitle);
    connect(m_context, &MVMEContext::daqConfigChanged, this, &mvme::onConfigChanged);
    connect(m_context, &MVMEContext::objectAboutToBeRemoved, this, &mvme::onObjectAboutToBeRemoved);
    connect(m_context, &MVMEContext::daqAboutToStart, this, &mvme::onDAQAboutToStart);

    // check and initialize VME interface
    VMEController *controller = new VMUSB;
    m_context->setController(controller);

    // create and initialize displays
    ui->setupUi(this);


    //
    // DAQControlWidget
    //
    {
        auto dock = new QDockWidget(QSL("DAQ Control"), this);
        dock_daqControl = dock;
        dock->setObjectName(QSL("DAQControlDock"));
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        dock->setWidget(new DAQControlWidget(m_context));
        //addDockWidget(Qt::LeftDockWidgetArea, dock);
    }

    //
    // DAQStatsWidget
    //
    {
        auto dock = new QDockWidget(QSL("DAQ Stats"), this);
        dock_daqStats = dock;
        dock->setObjectName(QSL("DAQStatsDock"));
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        auto widget = new DAQStatsWidget(m_context);
        dock->setWidget(widget);
        //addDockWidget(Qt::BottomDockWidgetArea, dock);
    }

    //
    // DAQConfigTreeWidget
    //
    m_daqConfigTreeWidget = new DAQConfigTreeWidget(m_context);

    {
        auto dock = new QDockWidget(QSL("VME Configuration"), this);
        dock_configTree = dock;
        dock->setObjectName("DAQConfigTreeWidgetDock");
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        dock->setWidget(m_daqConfigTreeWidget);
        //addDockWidget(Qt::LeftDockWidgetArea, dock);

        connect(m_daqConfigTreeWidget, &DAQConfigTreeWidget::configObjectClicked,
                this, &mvme::onObjectClicked);

        connect(m_daqConfigTreeWidget, &DAQConfigTreeWidget::configObjectDoubleClicked,
                this, &mvme::onObjectDoubleClicked);

        connect(m_daqConfigTreeWidget, &DAQConfigTreeWidget::showDiagnostics,
                this, &mvme::onShowDiagnostics);
    }

    //
    // HistogramTreeWidget
    //
    m_histogramTreeWidget = new HistogramTreeWidget(m_context);

    {
        auto dock = new QDockWidget(QSL("Analysis Config"), this);
        dock_histoTree = dock;
        dock->setObjectName("HistogramTreeWidgetDock");
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        dock->setWidget(m_histogramTreeWidget);
        //addDockWidget(Qt::LeftDockWidgetArea, dock);

        connect(m_histogramTreeWidget, &HistogramTreeWidget::objectClicked,
                this, &mvme::onObjectClicked);

        connect(m_histogramTreeWidget, &HistogramTreeWidget::objectDoubleClicked,
                this, &mvme::onObjectDoubleClicked);

        connect(m_histogramTreeWidget, &HistogramTreeWidget::openInNewWindow,
                this, &mvme::openInNewWindow);

        connect(m_histogramTreeWidget, &HistogramTreeWidget::showDiagnostics,
                this, &mvme::onShowDiagnostics);

        connect(m_histogramTreeWidget, &HistogramTreeWidget::addWidgetWindow,
                this, &mvme::addWidgetWindow);
    }

    //
    // Log Window
    //
    {
        m_logView->setWindowTitle("Log View");
        QFont font("MonoSpace");
        font.setStyleHint(QFont::Monospace);
        m_logView->setFont(font);
        m_logView->setTabChangesFocus(true);
        m_logView->document()->setMaximumBlockCount(10 * 1024 * 1024);
        m_logView->setContextMenuPolicy(Qt::CustomContextMenu);
        //m_logView->setStyleSheet("background-color: rgb(225, 225, 225);");
        connect(m_logView, &QWidget::customContextMenuRequested, this, [=](const QPoint &pos) {
            auto menu = m_logView->createStandardContextMenu(pos);
            auto action = menu->addAction("Clear");
            connect(action, &QAction::triggered, m_logView, &QTextBrowser::clear);
            menu->exec(m_logView->mapToGlobal(pos));
            menu->deleteLater();
        });

        auto logDock = new QDockWidget(QSL("Log View"));
        dock_logView = logDock;
        logDock->setObjectName("LogViewDock");
        logDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        logDock->setWidget(m_logView);
        //addDockWidget(Qt::BottomDockWidgetArea, logDock);
    }

    addDockWidget(Qt::LeftDockWidgetArea, dock_daqControl);
    addDockWidget(Qt::LeftDockWidgetArea, dock_configTree);
    addDockWidget(Qt::LeftDockWidgetArea, dock_histoTree);
    tabifyDockWidget(dock_configTree, dock_histoTree);

    addDockWidget(Qt::BottomDockWidgetArea, dock_daqStats);
    addDockWidget(Qt::BottomDockWidgetArea, dock_logView);

    resizeDocks({dock_daqControl, dock_configTree}, {1, 2}, Qt::Vertical);
    resizeDocks({dock_daqStats, dock_logView}, {1, 2}, Qt::Horizontal);

    dock_configTree->raise();

    //
    //
    //

    connect(m_context, &MVMEContext::sigLogMessage, this, &mvme::appendToLog);

    QSettings settings;

    // workspace
    if (settings.contains(QSL("LastWorkspaceDirectory")))
    {
        try
        {
            m_context->openWorkspace(settings.value(QSL("LastWorkspaceDirectory")).toString());
        } catch (const QString &e)
        {
            QMessageBox::critical(this, QSL("Workspace Error"), QString("Error opening workspace: %1").arg(e));
            settings.remove(QSL("LastWorkspaceDirectory"));
        }
    }
    else
    {
        on_actionNewWorkspace_triggered();
    }

    updateWindowTitle();

    //
    // FIXME: test code!
    //
    {
        auto widget = new analysis::AnalysisWidget(m_context);
        widget->setWindowTitle("Analysis UI");
        addWidgetWindow(widget);
    }
}

mvme::~mvme()
{

    auto workspaceDir = m_context->getWorkspaceDirectory();

    if (!workspaceDir.isEmpty())
    {
        QSettings settings;
        settings.setValue("LastWorkspaceDirectory", workspaceDir);
    }

    delete ui;
}

void mvme::loadConfig(const QString &fileName)
{
    m_context->loadVMEConfig(fileName);
}

void mvme::on_actionNewWorkspace_triggered()
{
    auto startDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    auto dirName  = QFileDialog::getExistingDirectory(this, QSL("Choose workspace directory"), startDir);

    if (dirName.isEmpty())
        return;

    try
    {
        m_context->newWorkspace(dirName);
    } catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Workspace Error"), QString("Error creating workspace: %1").arg(e));
    }
}

void mvme::on_actionOpenWorkspace_triggered()
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
        m_context->openWorkspace(dirName);
    } catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Workspace Error"), QString("Error opening workspace: %1").arg(e));
    }

#if 0
    QFileInfo workspaceFi(dirname + QSL("/mvmeworkspace.ini"));

    if (!workspaceFi.exists())
    {
        QMessageBox::critical(this, QSL("Workspace error"),
                              QSL("Not a valid workspace: mvmeworkspace.ini not found"));
        return;
    }

    if (!workspaceFi.isReadable())
    {
        QMessageBox::critical(this, QSL("Workspace error"),
                              QSL("Not a valid workspace: mvmeworkspace.ini not readable"));
        return;
    }
#endif
}

void mvme::displayAbout()
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

    {
        QString text = QString("mvme - %1").arg(GIT_VERSION);
        auto label = new QLabel;
        auto font = label->font();
        font.setPointSize(15);
        font.setBold(true);
        label->setFont(font);
        layout->addWidget(label);
    }

    layout->addWidget(new QLabel(QSL("mvme - VME Data Acquisition")));
    layout->addWidget(new QLabel(QString("Version %1").arg(GIT_VERSION)));
    layout->addWidget(new QLabel(QSL("© 2015-2017 mesytec GmbH & Co. KG")));
    layout->addWidget(new QLabel(QSL("Authors: F. Lüke, G. Montermann")));

    {
        QString text(QSL("<a href=\"mailto:info@mesytec.com\">info@mesytec.com</a> - <a href=\"http://www.mesytec.com\">www.mesytec.com</a>"));
        auto label = new QLabel(text);
        label->setOpenExternalLinks(true);
        layout->addWidget(label);
    }

    layout->addSpacing(20);

    auto buttonLayout = new QHBoxLayout;

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

void mvme::displayAboutQt()
{
    QMessageBox::aboutQt(this, QSL("About Qt"));
}

void mvme::closeEvent(QCloseEvent *event){
    if (m_context->getDAQState() != DAQState::Idle && m_context->getMode() == GlobalMode::DAQ)
    {
        QMessageBox msgBox(QMessageBox::Warning, QSL("DAQ is running"),
                           QSL("Data acquisition is currently active. Ignoring request to exit."),
                           QMessageBox::Ok);
        msgBox.exec();
        event->ignore();
        return;
    }

    // DAQConfig
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save DAQ configuration?"),
                           QSL("The current DAQ configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveConfig_triggered())
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

    // AnalysisConfig
    auto analysisConfig = m_context->getAnalysisConfig();
    if (analysisConfig->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, QSL("Save analysis config?"),
                           QSL("The current analysis configuration has modifications. Do you want to save it?"),
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            auto result = saveAnalysisConfig(analysisConfig,
                                             m_context->getAnalysisNG(),
                                             m_context->getAnalysisConfigFileName(),
                                             m_context->getWorkspaceDirectory());
            if (!result.first)
            {
                event->ignore();
                return;
            }
            m_context->setAnalysisConfigFileName(result.second);
        }
        else if (result == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
    }

    // window sizes and positions
    QSettings settings;
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());

    auto windowList = ui->mdiArea->subWindowList();

    settings.beginGroup("MdiSubWindows");

    for (auto subwin: windowList)
    {
        auto name = subwin->objectName();
        if (!name.isEmpty())
        {
            settings.setValue(name + "_size", subwin->size());
            settings.setValue(name + "_pos", subwin->pos());
        }
    }

    settings.endGroup();

    QMainWindow::closeEvent(event);
}

void mvme::restoreSettings()
{
    qDebug("restoreSettings");
    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

    auto windowList = ui->mdiArea->subWindowList();
    settings.beginGroup("MdiSubWindows");

    for (auto subwin: windowList)
    {
        auto name = subwin->objectName();

        auto size = settings.value(name + "_size").toSize();
        size = size.expandedTo(subwin->sizeHint());
        subwin->resize(size);

        QString pstr = name + "_pos";

        if (settings.contains(pstr))
        {
            subwin->move(settings.value(pstr).toPoint());
        }
    }

    settings.endGroup();
}

void mvme::on_actionNewConfig_triggered()
{
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save configuration?",
                           "The current configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    // TODO: run a dialog to set-up config basics: controller type, working directory, etc...

    m_context->setDAQConfig(new DAQConfig);
    m_context->setConfigFileName(QString());
    m_context->setMode(GlobalMode::DAQ);
}

void mvme::on_actionLoadConfig_triggered()
{
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save VME configuration?",
                           "The current VME configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    auto path = m_context->getWorkspaceDirectory();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load MVME Config",
                                                    path,
                                                    "MVME Config Files (*.mvmecfg);; All Files (*.*)");
    if (fileName.isEmpty())
        return;

    loadConfig(fileName);
}

bool mvme::on_actionSaveConfig_triggered()
{
    if (m_context->getConfigFileName().isEmpty())
    {
        return on_actionSaveConfigAs_triggered();
    }

    QString fileName = m_context->getConfigFileName();
    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    QJsonObject daqConfigJson;
    m_context->getDAQConfig()->write(daqConfigJson);
    QJsonObject configObject;
    configObject["DAQConfig"] = daqConfigJson;
    QJsonDocument doc(configObject);

    if (outFile.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    auto config = m_context->getConfig();
    config->setModified(false);
    auto configObjects = config->findChildren<ConfigObject *>();
    for (auto obj: configObjects)
    {
        obj->setModified(false);
    }

    updateWindowTitle();
    return true;
}

bool mvme::on_actionSaveConfigAs_triggered()
{
    QString path = QFileInfo(m_context->getConfigFileName()).absolutePath();

    if (path.isEmpty())
        path = m_context->getWorkspaceDirectory();

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getSaveFileName(this, "Save Config As", path,
                                                    "MVME Config Files (*.mvmecfg);; All Files (*.*)");

    if (fileName.isEmpty())
        return false;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".mvmecfg";
    }

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return false;
    }

    QJsonObject daqConfigJson;
    m_context->getDAQConfig()->write(daqConfigJson);
    QJsonObject configObject;
    configObject["DAQConfig"] = daqConfigJson;
    QJsonDocument doc(configObject);

    if (outFile.write(doc.toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return false;
    }

    m_context->setConfigFileName(fileName);
    m_context->getConfig()->setModified(false);
    updateWindowTitle();
    return true;
}

void mvme::on_actionOpen_Listfile_triggered()
{
    if (m_context->getConfig()->isModified())
    {
        QMessageBox msgBox(QMessageBox::Question, "Save configuration?",
                           "The current VME configuration has modifications. Do you want to save it?",
                           QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox.exec();

        if (result == QMessageBox::Save)
        {
            if (!on_actionSaveConfig_triggered())
            {
                return;
            }
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }


    QString path = m_context->getListFileDirectory();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load Listfile",
                                                    path,
                                                    "MVME Listfiles (*.mvmelst);; All Files (*.*)");
    if (fileName.isEmpty())
        return;

    ListFile *listFile = new ListFile(fileName);

    if (!listFile->open())
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for reading").arg(fileName));
        delete listFile;
        return;
    }

    auto json = listFile->getDAQConfig();

    if (json.isEmpty())
    {
        QMessageBox::critical(0, "Error", QString("Listfile does not contain a valid DAQ configuration"));
        delete listFile;
        return;
    }

    m_context->setListFile(listFile);
    updateWindowTitle();
}

void mvme::on_actionClose_Listfile_triggered()
{
    /* Open the last used VME config in the workspace. Create a new VME config
     * if no previous exists. */

    QString lastVMEConfig = m_context->makeWorkspaceSettings()->value(QSL("LastVMEConfig")).toString();

    if (!lastVMEConfig.isEmpty())
    {
        QDir wsDir(m_context->getWorkspaceDirectory());
        loadConfig(wsDir.filePath(lastVMEConfig));
    }
    else
    {
        m_context->setDAQConfig(new DAQConfig);
        m_context->setConfigFileName(QString());
        m_context->setMode(GlobalMode::DAQ);
    }
}

void mvme::on_actionVME_Debug_triggered()
{
    QMdiSubWindow *subwin = nullptr;

    for (auto win: ui->mdiArea->subWindowList())
    {
        if (qobject_cast<VMEDebugWidget *>(win->widget()))
        {
            subwin = win;
            break;
        }
    }

    if (!subwin)
    {
        auto widget = new VMEDebugWidget(m_context);
        subwin = new QMdiSubWindow(ui->mdiArea);
        subwin->setWindowIcon(QIcon(QPixmap(":/mesytec-window-icon.png")));
        subwin->setWidget(widget);
        subwin->setAttribute(Qt::WA_DeleteOnClose);
        ui->mdiArea->addSubWindow(subwin);
    }

    subwin->show();
    ui->mdiArea->setActiveSubWindow(subwin);
}

void mvme::openInNewWindow(QObject *object)
{
    auto scriptConfig      = qobject_cast<VMEScriptConfig *>(object);
    auto hist2d            = qobject_cast<Hist2D *>(object); 
    auto hist1d            = qobject_cast<Hist1D *>(object); 
    // The new histo type
    auto histo1d = qobject_cast<Histo1D *>(object);
    auto histo2d = qobject_cast<Histo2D *>(object);

    QWidget *widget = nullptr;
    QIcon windowIcon;
    QSize windowSize;

    if (scriptConfig)
    {
        widget = new VMEScriptEditor(m_context, scriptConfig);
        windowIcon = QIcon(QPixmap(":/vme_script.png"));
        windowSize = QSize(700, 450);
    }
    else if (hist1d)
    {
        auto histoConfig = qobject_cast<Hist1DConfig *>(m_context->getMappedObject(hist1d, QSL("ObjectToConfig")));
        widget = new Hist1DWidget(m_context, hist1d, histoConfig);
        windowIcon = QIcon(QPixmap(":/hist1d.png"));
        windowSize = QSize(600, 400);
    }
    else if (hist2d)
    {
        widget = new Hist2DWidget(m_context, hist2d);
        windowSize = QSize(600, 400);
    }
    else if (histo1d)
    {
        widget = new Histo1DWidget(histo1d);
        windowSize = QSize(600, 400);
    }
    else if (histo2d)
    {
        widget = new Histo2DWidget(histo2d);
        windowSize = QSize(600, 400);
    }

    if (windowIcon.isNull())
        windowIcon = QIcon(QPixmap(":/mesytec-window-icon.png"));

    if (widget)
    {
        widget->setAttribute(Qt::WA_DeleteOnClose);
        auto subwin = new QMdiSubWindow;
        subwin->setAttribute(Qt::WA_DeleteOnClose);
        subwin->setWidget(widget);

        if (!windowIcon.isNull())
            subwin->setWindowIcon(windowIcon);

        ui->mdiArea->addSubWindow(subwin);

        if (windowSize.isValid())
            subwin->resize(windowSize);

        subwin->show();
        ui->mdiArea->setActiveSubWindow(subwin);

        qDebug() << "adding window" << subwin << "for object" << object;

        m_objectWindows[object].push_back(subwin);

        if (auto mvmeWidget = qobject_cast<MVMEWidget *>(widget))
        {
            connect(mvmeWidget, &MVMEWidget::aboutToClose, this, [this, object, subwin] {
                qDebug() << "removing window" << subwin << "for object" << object;
                m_objectWindows[object].removeOne(subwin);
                subwin->close();
            });
        }
    }
}

void mvme::addWidgetWindow(QWidget *widget, QSize windowSize)
{
    auto windowIcon = QIcon(QPixmap(":/mesytec-window-icon.png"));
    widget->setAttribute(Qt::WA_DeleteOnClose);
    auto subwin = new QMdiSubWindow;
    subwin->setAttribute(Qt::WA_DeleteOnClose);
    subwin->setWidget(widget);


    subwin->setWindowIcon(windowIcon);

    ui->mdiArea->addSubWindow(subwin);

    if (windowSize.isValid())
        subwin->resize(windowSize);
    else
        subwin->resize(QSize(600, 400));

    subwin->show();
    ui->mdiArea->setActiveSubWindow(subwin);

    auto mvmeWidget = qobject_cast<MVMEWidget *>(widget);

    if (mvmeWidget)
    {
        connect(mvmeWidget, &MVMEWidget::aboutToClose, this, [subwin]() {
            subwin->close();
        });
    }
}

void mvme::onObjectClicked(QObject *object)
{
    auto &lst = m_objectWindows[object];

    if (!lst.isEmpty())
    {
        auto window = lst.last();
        if (window)
        {
            window->show();
            window->showNormal();
            window->activateWindow();
            window->raise();
            ui->mdiArea->setActiveSubWindow(window);
        }
    }
}

void mvme::onObjectDoubleClicked(QObject *object)
{
    if (!m_objectWindows[object].isEmpty())
    {
        onObjectClicked(object);
    }
    else
    {
        openInNewWindow(object);
    }
}

void mvme::onObjectAboutToBeRemoved(QObject *object)
{
    auto &windowList = m_objectWindows[object];

    qDebug() << __PRETTY_FUNCTION__ << object << windowList;

    for (auto subwin: windowList)
        subwin->close();

    m_objectWindows.remove(object);
}

void mvme::appendToLog(const QString &s)
{
  auto str(QString("%1: %2")
      .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
      .arg(s));

  m_logView->append(str);
  auto bar = m_logView->verticalScrollBar();
  bar->setValue(bar->maximum());
  auto debug(qDebug());
  debug.noquote();
  debug << str;
}

void mvme::updateWindowTitle()
{
    QString workspaceDir = m_context->getWorkspaceDirectory();
    workspaceDir.replace(QDir::homePath(), QSL("~"));

    QString title;
    switch (m_context->getMode())
    {
        case GlobalMode::DAQ:
            {
                title = QString("%1 - [DAQ mode] - mvme")
                    .arg(workspaceDir);
            } break;

        case GlobalMode::ListFile:
            {
                auto listFile = m_context->getListFile();
                QString fileName(QSL("<no listfile>"));
                if (listFile)
                {
                    QString filePath = m_context->getListFile()->getFileName();
                    fileName =  QFileInfo(filePath).fileName();
                }

                title = QString("%1 - %2 - [ListFile mode] - mvme")
                    .arg(workspaceDir)
                    .arg(fileName);
            } break;

        case GlobalMode::NotSet:
            break;
    }

    if (m_context->getMode() == GlobalMode::DAQ && m_context->isWorkspaceModified())
    {
        title += " *";
    }

    setWindowTitle(title);
}

void mvme::onConfigChanged(DAQConfig *config)
{
    connect(config, &DAQConfig::modifiedChanged, this, &mvme::updateWindowTitle);
    updateWindowTitle();
}

void mvme::clearLog()
{
    m_logView->clear();
}

void mvme::resizeEvent(QResizeEvent *event)
{
    resizeDocks({dock_daqControl, dock_configTree}, {1, 10}, Qt::Vertical);
    resizeDocks({dock_daqStats, dock_logView}, {1, 10}, Qt::Horizontal);
    QMainWindow::resizeEvent(event);
}

void mvme::onDAQAboutToStart(quint32 nCycles)
{
    QList<VMEScriptEditor *> scriptEditors;

    for (auto windowList: m_objectWindows.values())
    {
        for (auto window: windowList)
        {
            if (auto scriptEditor = qobject_cast<VMEScriptEditor *>(window->widget()))
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

void mvme::onShowDiagnostics(ModuleConfig *moduleConfig)
{
    if (m_context->getEventProcessor()->getDiagnostics())
        return;

    auto diag   = new MesytecDiagnostics;
    diag->setEventAndModuleIndices(m_context->getDAQConfig()->getEventAndModuleIndices(moduleConfig));

    auto widget = new MesytecDiagnosticsWidget(diag);
    

    auto eventProcessor = m_context->getEventProcessor();
    eventProcessor->setDiagnostics(diag);
    // XXX: moveToThread?

    connect(widget, &MVMEWidget::aboutToClose, this, [this]() {
        QMetaObject::invokeMethod(m_context->getEventProcessor(), "removeDiagnostics", Qt::QueuedConnection);
    });

    connect(m_context, &MVMEContext::daqStateChanged, widget, [this, widget] (const DAQState &state) {
        if (state == DAQState::Running)
        {
            widget->clearResultsDisplay();
        }

    });

    auto subwin = new QMdiSubWindow(ui->mdiArea);
    subwin->setWidget(widget);
    subwin->setAttribute(Qt::WA_DeleteOnClose);
    subwin->resize(1024, 760);
    ui->mdiArea->addSubWindow(subwin);
    subwin->show();
    ui->mdiArea->setActiveSubWindow(subwin);
}

void mvme::on_actionImport_Histogram_triggered()
{
    auto path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QString fileName = QFileDialog::getOpenFileName(this, QSL("Import Histogram"),
                                                    path,
                                                    "Text files (*.txt);; All Files (*.*)");
    if (fileName.isEmpty())
        return;

    QFile inFile(fileName);

    if (inFile.open(QIODevice::ReadOnly))
    {
        QTextStream inStream(&inFile);
        auto histo = readHistogram(inStream);

        if (histo)
        {
            auto widget = new Hist1DWidget(m_context, histo);
            histo->setParent(widget);
            histo->setObjectName(fileName);
            widget->setAttribute(Qt::WA_DeleteOnClose);
            auto subwin = new QMdiSubWindow;
            subwin->setAttribute(Qt::WA_DeleteOnClose);
            subwin->setWidget(widget);
            ui->mdiArea->addSubWindow(subwin);
            subwin->show();
            ui->mdiArea->setActiveSubWindow(subwin);
        }
    }
}

void mvme::on_actionImport_Histo1D_triggered()
{
    QSettings settings;
    QString path = settings.value(QSL("LastHisto1DDirectory")).toString();

    if (path.isEmpty())
    {
        path = m_context->getWorkspaceDirectory();
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

            auto subwin = new QMdiSubWindow;
            subwin->setAttribute(Qt::WA_DeleteOnClose);

            subwin->setAttribute(Qt::WA_DeleteOnClose);
            subwin->setWidget(widget);
            ui->mdiArea->addSubWindow(subwin);
            subwin->show();
            ui->mdiArea->setActiveSubWindow(subwin);


            if (path != m_context->getWorkspaceDirectory())
            {
                settings.setValue(QSL("LastHisto1DDirectory"), path);
            }
            else
            {
                settings.remove(QSL("LastHisto1DDirectory"));
            }
        }
    }
}

void mvme::on_actionVMEScriptRef_triggered()
{
    auto widget = make_vme_script_ref_widget();
    addWidgetWindow(widget);
}
