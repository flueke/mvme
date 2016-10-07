#include "mvme.h"
#include "ui_mvme.h"
#include "vmusb.h"
#include "mvme_context.h"
#include "twodimwidget.h"
#include "hist2d.h"
#include "histogram.h"
#include "mvmedefines.h"
#include "mvme_context_widget.h"
#include "vmusb_readout_worker.h"
#include "config_widgets.h"
#include "mvme_listfile.h"
#include "mvmecontrol.h"
#include "daqconfig_tree.h"
#include "vme_script_editor.h"

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

    connect(m_context, &MVMEContext::configFileNameChanged, this, &mvme::updateWindowTitle);
    connect(m_context, &MVMEContext::configChanged, this, &mvme::onConfigChanged);

    // check and initialize VME interface
    VMEController *controller = new VMUSB;
    m_context->setController(controller);

    // create and initialize displays
    ui->setupUi(this);
    auto contextWidget = new MVMEContextWidget(m_context);
    m_contextWidget = contextWidget;


    connect(contextWidget, &MVMEContextWidget::histogramCollectionClicked, this, &mvme::handleHistogramCollectionClicked);
    connect(contextWidget, &MVMEContextWidget::histogramCollectionDoubleClicked, this, &mvme::handleHistogramCollectionDoubleClicked);
    connect(contextWidget, &MVMEContextWidget::showHistogramCollection, this, &mvme::openHistogramView);
    connect(contextWidget, &MVMEContextWidget::hist2DClicked, this, &mvme::handleHist2DClicked);
    connect(contextWidget, &MVMEContextWidget::hist2DDoubleClicked, this, &mvme::handleHist2DDoubleClicked);
    connect(contextWidget, &MVMEContextWidget::showHist2D, this, &mvme::openHist2DView);

    connect(m_context, &MVMEContext::hist2DAboutToBeRemoved, this, [=](Hist2D *hist2d) {
        for (auto subwin: ui->mdiArea->subWindowList())
        {
            auto w = qobject_cast<Hist2DWidget *>(subwin->widget());
            if (w && w->getHist2D() == hist2d)
            {
                qDebug() << subwin;
                subwin->close();
            }
        }
    });

    connect(m_context, &MVMEContext::modeChanged, this, &mvme::updateWindowTitle);

    auto contextDock = new QDockWidget("Old Dock");
    contextDock->setObjectName("MVMEContextDock");
    contextDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    contextDock->setWidget(contextWidget);
    addDockWidget(Qt::LeftDockWidgetArea, contextDock);


    //
    // DAQConfigTreeWidget
    //
    m_daqConfigTreeWidget = new DAQConfigTreeWidget(m_context);

    {
        auto dock = new QDockWidget(QSL("VME Configuration"), this);
        dock->setObjectName("DAQConfigTreeWidgetDock");
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        dock->setWidget(m_daqConfigTreeWidget);
        addDockWidget(Qt::LeftDockWidgetArea, dock);

        connect(m_context, &MVMEContext::configChanged, m_daqConfigTreeWidget, &DAQConfigTreeWidget::setConfig);

        connect(m_daqConfigTreeWidget, &DAQConfigTreeWidget::configObjectClicked,
                this, &mvme::onConfigObjectClicked);

        connect(m_daqConfigTreeWidget, &DAQConfigTreeWidget::configObjectDoubleClicked,
                this, &mvme::onConfigObjectDoubleClicked);
    }

    //
    // Log Window
    //
    m_logView->setWindowTitle("Log Window");
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

#if 1
    m_logViewSubwin = new QMdiSubWindow;
    m_logViewSubwin->setObjectName("LogViewWindow");
    m_logViewSubwin->setWidget(m_logView);
    m_logViewSubwin->setAttribute(Qt::WA_DeleteOnClose, false);
    ui->mdiArea->addSubWindow(m_logViewSubwin);
#else
    auto logDock = new QDockWidget();
    logDock->setObjectName("MVMEContextDock");
    logDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    logDock->setWidget(m_logView);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
#endif


    drawTimer = new QTimer(this);
    connect(drawTimer, SIGNAL(timeout()), SLOT(drawTimerSlot()));
    drawTimer->start(DrawTimerInterval);

    connect(m_context, &MVMEContext::sigLogMessage, this, &mvme::appendToLog);

    QSettings settings;

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

#if 0
void mvme::closeConfig()
{

    for (auto win: ui->mdiArea->subWindowList())
    {
        if(qobject_cast<EventConfigWidget *>(win->widget()) ||
           qobject_cast<ModuleConfigDialog *>(win->widget()))
        {
            win->close();
        }
    }
}
#endif

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

    m_context->read(doc.object());
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
    if (m_context->getDAQState() != DAQState::Idle)
    {
        QMessageBox msgBox(QMessageBox::Warning, QSL("DAQ is running"),
                           QSL("Data acquisition is currently active. Ignoring request to exit."),
                           QMessageBox::Ok);
        msgBox.exec();
        event->ignore();
        return;
    }

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

    m_context->setConfig(new DAQConfig);
    m_context->setConfigFileName(QString());
    m_context->setMode(GlobalMode::DAQ);

    m_context->removeHistogramCollections();
    m_context->remove2DHistograms();
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

    QJsonObject configObject;
    m_context->write(configObject);
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

    QJsonObject configObject;
    m_context->write(configObject);
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

void mvme::on_actionShowLogWindow_triggered()
{
    m_logViewSubwin->widget()->show();
    m_logViewSubwin->show();
    m_logViewSubwin->showNormal();
    m_logViewSubwin->raise();
}

void mvme::onConfigObjectClicked(ConfigObject *config)
{
    if (m_configWindows.contains(config))
    {
        m_configWindows[config]->show();
        m_configWindows[config]->showNormal();
        m_configWindows[config]->activateWindow();
        m_configWindows[config]->raise();
        ui->mdiArea->setActiveSubWindow(m_configWindows[config]);
    }
}

void mvme::onConfigObjectDoubleClicked(ConfigObject *config)
{
    if (m_configWindows.contains(config))
    {
        m_configWindows[config]->show();
        m_configWindows[config]->showNormal();
        m_configWindows[config]->activateWindow();
        m_configWindows[config]->raise();
        ui->mdiArea->setActiveSubWindow(m_configWindows[config]);
    }
    else
    {
        auto scriptConfig = qobject_cast<VMEScriptConfig *>(config);
        auto eventConfig = qobject_cast<EventConfig *>(config);
        auto moduleConfig = qobject_cast<ModuleConfig *>(config);

        MVMEWidget *widget = nullptr;

        if (scriptConfig)
        {
            widget = new VMEScriptEditor(m_context, scriptConfig);
        }

        if (widget)
        {
            widget->setAttribute(Qt::WA_DeleteOnClose);
            auto subwin = new QMdiSubWindow(ui->mdiArea);
            subwin->setAttribute(Qt::WA_DeleteOnClose);
            subwin->setWidget(widget);

            m_configWindows[config] = subwin;

            connect(widget, &MVMEWidget::aboutToClose, this, [this, config] {
                m_configWindows[config]->close();
                m_configWindows.remove(config);
            });

            subwin->show();
        }
    }
}

void mvme::handleHistogramCollectionClicked(HistogramCollection *histo)
{
    qDebug() << histo << histo->property("Histogram.sourceModule").toUuid();


    QMdiSubWindow *subwin = 0;
    for (auto win: ui->mdiArea->subWindowList())
    {
        auto widget = qobject_cast<TwoDimWidget *>(win->widget());
        if (widget && widget->getHistogram() == histo)
        {
            subwin = win;
            break;
        }
    }

    if (subwin)
    {
        subwin->show();
        if (subwin->isMinimized())
            subwin->showNormal();
        subwin->raise();
    }
    ui->mdiArea->setActiveSubWindow(subwin);
}

void mvme::handleHistogramCollectionDoubleClicked(HistogramCollection *histo)
{
    for (auto win: ui->mdiArea->subWindowList())
    {
        auto widget = qobject_cast<TwoDimWidget *>(win->widget());
        if (widget && widget->getHistogram() == histo)
        {
            return;
        }
    }

    openHistogramView(histo);
}

void mvme::openHistogramView(HistogramCollection *histo)
{
    if (histo)
    {
        auto widget = new TwoDimWidget(m_context, histo);
        auto subwin = new QMdiSubWindow(ui->mdiArea);
        subwin->setWidget(widget);
        subwin->setAttribute(Qt::WA_DeleteOnClose);
        ui->mdiArea->addSubWindow(subwin);
        subwin->show();
        ui->mdiArea->setActiveSubWindow(subwin);
    }
}

void mvme::handleHist2DClicked(Hist2D *hist2d)
{
    QMdiSubWindow *subwin = 0;
    for (auto win: ui->mdiArea->subWindowList())
    {
        auto widget = qobject_cast<Hist2DWidget *>(win->widget());
        if (widget && widget->getHist2D() == hist2d)
        {
            subwin = win;
            break;
        }
    }

    if (subwin)
    {
        subwin->show();
        if (subwin->isMinimized())
            subwin->showNormal();
        subwin->raise();
    }
    ui->mdiArea->setActiveSubWindow(subwin);
}

void mvme::handleHist2DDoubleClicked(Hist2D *hist2d)
{
    for (auto win: ui->mdiArea->subWindowList())
    {
        auto widget = qobject_cast<Hist2DWidget *>(win->widget());
        if (widget && widget->getHist2D() == hist2d)
        {
            return;
        }
    }

    openHist2DView(hist2d);
}

void mvme::openHist2DView(Hist2D *hist2d)
{
    if (hist2d)
    {
        auto widget = new Hist2DWidget(m_context, hist2d);
        auto subwin = new QMdiSubWindow(ui->mdiArea);
        subwin->setWidget(widget);
        subwin->setAttribute(Qt::WA_DeleteOnClose);
        ui->mdiArea->addSubWindow(subwin);
        subwin->show();
        ui->mdiArea->setActiveSubWindow(subwin);
    }
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
                modeString = "DAQ mode";
            } break;
        case GlobalMode::ListFile:
            {
                auto listFile = m_context->getListFile();
                if (listFile)
                {
                    QString filePath = m_context->getListFile()->getFileName();
                    fileName =  QFileInfo(filePath).fileName();
                }
                modeString = "ListFile mode";
            } break;
        case GlobalMode::NotSet:
            modeString = "Unknown mode";
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
    m_contextWidget->reloadConfig();
}

void mvme::on_actionShow_MVME_Control_triggered()
{
    auto subwins = getSubwindowsByWidgetType<mvmeControl>(ui->mdiArea);
    if (!subwins.isEmpty())
    {
        auto subwin = subwins.at(0);
        subwin->showNormal();
        subwin->raise();
    }
    else
    {
        auto mvme_control = new mvmeControl(this);
        auto subwin = new QMdiSubWindow;
        subwin->setObjectName("MVMEControlSubWindow");
        subwin->setWidget(mvme_control);
        subwin->setAttribute(Qt::WA_DeleteOnClose, true);
        subwin->resize(800, 600);
        ui->mdiArea->addSubWindow(subwin);
    }
}
