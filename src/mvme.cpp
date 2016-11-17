#include "mvme.h"

#include "ui_mvme.h"
#include "vmusb.h"
#include "mvme_context.h"
#include "twodimwidget.h"
#include "hist1d.h"
#include "hist2d.h"
#include "histogram.h"
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

#include <QDockWidget>
#include <QFileDialog>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QtGui>
#include <QTimer>
#include <QToolBar>
#include <QTextEdit>
#include <QFont>
#include <QList>
#include <QTextBrowser>
#include <QScrollBar>

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

static const int DrawTimerInterval = 1000;

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
    , m_context(new MVMEContext(this))
    , m_logView(new QTextBrowser)
{
    qDebug() << "main thread: " << QThread::currentThread();

    connect(m_context, &MVMEContext::daqConfigFileNameChanged, this, &mvme::updateWindowTitle);
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

        connect(m_context, &MVMEContext::daqConfigChanged, m_daqConfigTreeWidget, &DAQConfigTreeWidget::setConfig);

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
    drawTimer = new QTimer(this);
    connect(drawTimer, SIGNAL(timeout()), SLOT(drawTimerSlot()));
    drawTimer->start(DrawTimerInterval);

    connect(m_context, &MVMEContext::sigLogMessage, this, &mvme::appendToLog);

    QSettings settings;

    // DAQConfig
    if (settings.contains("Files/LastConfigFile"))
    {
        auto configFileName = settings.value("Files/LastConfigFile").toString();
        qDebug() << "LastConfigFile" << configFileName;
        if (!loadConfig(configFileName))
        {
            settings.remove("Files/LastConfigFile");
        }
    }
    else
    {
        // try to load a default config file
        auto configFileName = QCoreApplication::applicationDirPath() + QSL("/default.mvmecfg");
        QFileInfo fi(configFileName);

        qDebug() << "default config" << configFileName;

        if (fi.exists() && fi.isReadable())
        {
            loadConfig(configFileName);

            // don't use the default filename for saving
            m_context->setConfigFileName(QString());
            QSettings settings;
            settings.setValue("Files/LastConfigFile", QString());
        }
    }
    
    // AnalysisConfig
    {
        const QString settingsPath = QSL("Files/LastAnalysisConfig");
        if (settings.contains(settingsPath))
        {
            auto fileName = settings.value(settingsPath).toString();
            auto doc = gui_read_json_file(fileName);
            if (doc.isNull())
            {
                settings.remove(settingsPath);
            }
            else
            {
                auto analysisConfig = new AnalysisConfig;
                analysisConfig->read(doc.object()[QSL("AnalysisConfig")].toObject());
                m_context->setAnalysisConfig(analysisConfig);
                m_context->setAnalysisConfigFileName(fileName);
            }
        }
    }

    updateWindowTitle();
}

mvme::~mvme()
{
    QSettings settings;
    if (!m_context->getConfigFileName().isEmpty())
    {
        settings.setValue("Files/LastConfigFile", m_context->getConfigFileName());
    }

    delete ui;
    delete m_context;
}

bool mvme::loadConfig(const QString &fileName)
{
    qDebug() << __PRETTY_FUNCTION__ << fileName;

    if (fileName.isEmpty())
    {
        return false;
    }

    QFile inFile(fileName);
    if (!inFile.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error reading from %1").arg(fileName));
        return false;
    }

    auto data = inFile.readAll();

    QJsonParseError parseError;
    QJsonDocument doc(QJsonDocument::fromJson(data, &parseError));

    if (parseError.error != QJsonParseError::NoError)
    {
        QMessageBox::critical(0, "Error", QString("Error reading from %1: %2 at offset %3")
                              .arg(fileName)
                              .arg(parseError.errorString())
                              .arg(parseError.offset)
                             );
        return false;
    }

    auto daqConfig = new DAQConfig;
    daqConfig->read(doc.object()["DAQConfig"].toObject());

    m_context->setDAQConfig(daqConfig);
    m_context->setConfigFileName(fileName);
    m_context->setMode(GlobalMode::DAQ);

    QSettings settings;
    settings.setValue("Files/LastConfigFile", fileName);

    return true;
}

void mvme::replot()
{
    foreach(QMdiSubWindow *w, ui->mdiArea->subWindowList())
    {
        auto tdw = qobject_cast<TwoDimWidget *>(w->widget());
        if (tdw)
        {
            tdw->plot();
        }
    }
}

void mvme::drawTimerSlot()
{
    replot();
}

void mvme::displayAbout()
{
    QMessageBox::about(this, tr("about mvme"), tr("mvme by G. Montermann, mesytec GmbH & Co. KG"));
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
            if (!saveAnalysisConfig(analysisConfig, m_context->getAnalysisConfigFileName()))
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
#if 1
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
#endif
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

    QString path = QFileInfo(QSettings().value("Files/LastConfigFile").toString()).absolutePath();

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
    QString path = QFileInfo(QSettings().value("Files/LastConfigFile").toString()).absolutePath();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

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
    QSettings().setValue("Files/LastConfigFile", fileName);
    updateWindowTitle();
    return true;
}

void mvme::on_actionOpen_Listfile_triggered()
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


    QSettings settings;
    QString path = QFileInfo(settings.value("Files/LastListFile").toString()).absolutePath();

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

    settings.setValue("Files/LastListFile", fileName);
    m_context->setListFile(listFile);
}

void mvme::on_actionVME_Debug_triggered()
{
    auto widget = new VMEDebugWidget(m_context);
    auto subwin = new QMdiSubWindow(ui->mdiArea);
    subwin->setWidget(widget);
    subwin->setAttribute(Qt::WA_DeleteOnClose);
    ui->mdiArea->addSubWindow(subwin);
    subwin->show();
    ui->mdiArea->setActiveSubWindow(subwin);
}

void mvme::openInNewWindow(QObject *object)
{
    auto scriptConfig       = qobject_cast<VMEScriptConfig *>(object);
    auto histoCollection    = qobject_cast<HistogramCollection *>(object); 
    auto histo2d            = qobject_cast<Hist2D *>(object); 
    auto histo1d            = qobject_cast<Hist1D *>(object); 

    MVMEWidget *widget = nullptr;

    if (scriptConfig)
    {
        widget = new VMEScriptEditor(m_context, scriptConfig);
    }
    else if (histoCollection)
    {
        widget = new TwoDimWidget(histoCollection);
    }
    else if (histo1d)
    {
        auto histoConfig = qobject_cast<Hist1DConfig *>(m_context->getMappedObject(histo1d, QSL("ObjectToConfig")));
        widget = new Hist1DWidget(m_context, histo1d, histoConfig);
    }
    else if (histo2d)
    {
        widget = new Hist2DWidget(m_context, histo2d);
    }

    if (widget)
    {
        widget->setAttribute(Qt::WA_DeleteOnClose);
        auto subwin = new QMdiSubWindow;
        subwin->setAttribute(Qt::WA_DeleteOnClose);
        subwin->setWidget(widget);
        ui->mdiArea->addSubWindow(subwin);
        subwin->show();
        ui->mdiArea->setActiveSubWindow(subwin);

        qDebug() << "adding window" << subwin << "for object" << object;

        m_objectWindows[object].push_back(subwin);

        connect(widget, &MVMEWidget::aboutToClose, this, [this, object, subwin] {
            qDebug() << "removing window" << subwin << "for object" << object;
            m_objectWindows[object].removeOne(subwin);
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
    QString title;
    QString modeString;
    QString fileName;

    switch (m_context->getMode())
    {
        case GlobalMode::DAQ:
            {
                QString filePath = m_context->getConfigFileName();
                fileName =  QFileInfo(filePath).fileName();
                modeString = QSL("DAQ mode");
            } break;
        case GlobalMode::ListFile:
            {
                auto listFile = m_context->getListFile();
                if (listFile)
                {
                    QString filePath = m_context->getListFile()->getFileName();
                    fileName =  QFileInfo(filePath).fileName();
                }
                modeString = QSL("ListFile mode");
            } break;

        case GlobalMode::NotSet:
            break;
    }

    if (!fileName.isEmpty())
    {
        title = QString("%1 [%2] - mvme2")
                 .arg(fileName)
                 .arg(modeString)
                 ;
    }
    else
    {
        title = QString("[%2] - mvme2")
            .arg(modeString)
            ;
    }

    if (m_context->getMode() == GlobalMode::DAQ && m_context->getConfig()->isModified())
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
    diag->setParent(widget);

    // TODO: use QMetaObject::invokeMethod to execute in the event processors thread!
    m_context->getEventProcessor()->setDiagnostics(diag);
    connect(widget, &MVMEWidget::aboutToClose, this, [this]() {
        m_context->getEventProcessor()->setDiagnostics(nullptr);
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
