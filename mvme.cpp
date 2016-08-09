#include "mvme.h"
#include "ui_mvme.h"
#include "vmusb.h"
#include "mvmecontrol.h"
#include "ui_mvmecontrol.h" // FIXME
#include "mvme_context.h"
#include "twodimwidget.h"
#include "diagnostics.h"
#include "realtimedata.h"
#include "channelspectro.h"
#include "histogram.h"
#include "datacruncher.h"
#include "datathread.h"
#include "mvmedefines.h"
#include "mvme_context_widget.h"
#include "ui_moduleconfig_widget.h"
#include "vmusb_readout_worker.h"
#include "config_widgets.h"

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
    vu(0),
    mctrl(0),
    dt(0),
    dc(0),
    diag(0),
    rd(0),
    m_channelSpectro(new ChannelSpectro(1024, 1024)),
    ui(new Ui::mvme)
    , m_context(new MVMEContext(this))
    , m_logView(new QTextBrowser)
{

    qDebug() << "main thread: " << QThread::currentThread();

    m_histogram[0] = new Histogram(this, 42, 8192);
    m_histogram[0]->initHistogram();

    m_channelSpectro->setXAxisChannel(0);
    m_channelSpectro->setYAxisChannel(1);

    connect(m_context, &MVMEContext::configFileNameChanged, this, &mvme::updateWindowTitle);
    connect(m_context, &MVMEContext::configChanged, this, &mvme::onConfigChanged);
            
            
    // create and initialize displays
    ui->setupUi(this);
    auto contextWidget = new MVMEContextWidget(m_context);
    m_contextWidget = contextWidget;
    connect(contextWidget, &MVMEContextWidget::eventClicked, this, &mvme::handleEventConfigClicked);
    connect(contextWidget, &MVMEContextWidget::moduleClicked, this, &mvme::handleModuleConfigClicked);
    connect(contextWidget, &MVMEContextWidget::moduleDoubleClicked, this, &mvme::handleModuleConfigDoubleClicked);

    connect(contextWidget, &MVMEContextWidget::deleteEvent, this, &mvme::handleDeleteEventConfig);
    connect(contextWidget, &MVMEContextWidget::deleteModule, this, &mvme::handleDeleteModuleConfig);

    connect(contextWidget, &MVMEContextWidget::histogramClicked, this, &mvme::handleHistogramClicked);
    connect(contextWidget, &MVMEContextWidget::histogramDoubleClicked, this, &mvme::handleHistogramDoubleClicked);
    connect(contextWidget, &MVMEContextWidget::showHistogram, this, &mvme::openHistogramView);

    auto contextDock = new QDockWidget();
    contextDock->setObjectName("MVMEContextDock");
    contextDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    contextDock->setWidget(contextWidget);
    addDockWidget(Qt::LeftDockWidgetArea, contextDock);

    //createNewHistogram();
    //createNewChannelSpectrogram();

    rd = new RealtimeData;
    diag = new Diagnostics;

    // check and initialize VME interface
    vu = new VMUSB;
    m_context->setController(vu);
    vu->getUsbDevices();
    vu->openFirstUsbDevice();


    mctrl = new mvmeControl(this);
    mctrl->show();
#if 0
    {
        auto subwin = new QMdiSubWindow(ui->mdiArea);
        subwin->setWidget(mctrl);
        subwin->resize(mctrl->size());
        subwin->show();
    }
#endif

    // read current configuration
    mctrl->getValues();

    drawTimer = new QTimer(this);
    connect(drawTimer, SIGNAL(timeout()), SLOT(drawTimerSlot()));
    drawTimer->start(DrawTimerInterval);

    //initThreads();

    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

    m_logView->setWindowTitle("Log View");
    QFont font("MonoSpace");
    font.setStyleHint(QFont::Monospace);
    m_logView->setFont(font);
    m_logView->setTabChangesFocus(true);
    m_logView->document()->setMaximumBlockCount(10 * 1024 * 1024);
    m_logView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_logView, &QWidget::customContextMenuRequested, this, &mvme::handleLogViewContextMenu);

    m_logViewSubwin = new QMdiSubWindow;
    m_logViewSubwin->setWidget(m_logView);
    m_logViewSubwin->setAttribute(Qt::WA_DeleteOnClose, false);
    ui->mdiArea->addSubWindow(m_logViewSubwin);

    //connect(m_context->m_dataProcessor, &DataProcessor::eventFormatted,
    //        textView, &QTextEdit::append);
    connect(m_context, &MVMEContext::logMessage, this, &mvme::appendToLog);
    connect(m_context, &MVMEContext::daqStateChanged, this, [=](DAQState state) {
        if (state == DAQState::Starting)
        {
            appendToLog("DAQ starting");
        }
        if (state == DAQState::Idle)
        {
            appendToLog("DAQ stopped");
        }
    });


    auto configFileName = settings.value("Files/LastConfigFile").toString();

    if (!configFileName.isEmpty())
    {
        if (!loadConfig(configFileName))
        {
            settings.remove("Files/LastConfigFile");
        }
    }
}

mvme::~mvme()
{
    QSettings settings;
    settings.setValue("Files/LastConfigFile", m_context->getConfigFileName());

    delete vu;
    delete mctrl;
    delete ui;
//    delete hist;
    delete m_histogram.value(0);
    delete dt;
    delete dc;
    delete rd;
    delete m_channelSpectro;
    delete m_context;
}

//void mvme::closeConfig()
//{
//    for (auto win: ui->mdiArea->subWindowList())
//    {
//        if(qobject_cast<EventConfigWidget *>(win->widget()) ||
//           qobject_cast<ModuleConfigWidget *>(win->widget()))
//        {
//            win->close();
//        }
//    }
//}

bool mvme::loadConfig(const QString &fileName)
{
    QFile inFile(fileName);
    if (!inFile.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error reading from %1").arg(fileName));
        return false;
    }

    for (auto win: ui->mdiArea->subWindowList())
    {
        if(qobject_cast<EventConfigWidget *>(win->widget()) ||
           qobject_cast<ModuleConfigWidget *>(win->widget()))
        {
            win->close();
        }
    }

    auto data = inFile.readAll();
    QJsonDocument doc(QJsonDocument::fromJson(data));
    auto config = new DAQConfig;
    config->read(doc.object());
    m_context->setConfig(config);
    m_context->setConfigFileName(fileName);

    QSettings settings;
    settings.setValue("Files/LastConfigFile", fileName);

    m_contextWidget->reloadConfig();

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
    rd->calcData();
    mctrl->dispRt();
}

void mvme::displayAbout()
{
    QMessageBox::about(this, tr("about mvme"), tr("mvme by G. Montermann, mesytec GmbH & Co. KG"));
}

void mvme::createNewHistogram()
{
    auto tdw = new TwoDimWidget(m_context, m_histogram.value(0));
    tdw->plot();

    auto subwin = new QMdiSubWindow(ui->mdiArea);
    subwin->setWidget(tdw);
    subwin->show();
}

void mvme::createNewChannelSpectrogram()
{
    auto subwin = new QMdiSubWindow(ui->mdiArea);
    auto channelSpectroWidget = new ChannelSpectroWidget(m_channelSpectro);
    subwin->setWidget(channelSpectroWidget);
    subwin->show();
}

#if 1
void mvme::startDatataking(quint16 period, bool multi, quint16 readLen, bool mblt, bool daqMode)
{
    QString outputFileName = mctrl->getOutputFileName();

    if (!outputFileName.isNull())
    {
        QFile *outputFile = new QFile(outputFileName);
        outputFile->open(QIODevice::WriteOnly);
        dt->setOutputFile(outputFile);
    }

    QString inputFileName = mctrl->getInputFileName();

    if (!inputFileName.isNull())
    {
        QFile *inputFile = new QFile(inputFileName);
        inputFile->open(QIODevice::ReadOnly);
        dt->setInputFile(inputFile);
    }

    dt->setReadoutmode(multi, readLen, mblt, daqMode);
    dt->startReading(period);

    datataking = true;
}

void mvme::stopDatataking()
{
    QTime timer;
    timer.start();

    dt->stopReading();
    drawTimer->stop();
    datataking = false;
    qDebug() << __PRETTY_FUNCTION__ << "elapsed:" << timer.elapsed();
}

void mvme::initThreads()
{
#if 0
    dt = new DataThread;
    dt->setVu(vu);

    dt->moveToThread(m_readoutThread);

    //dt->setCu(cu);
    dc = new DataCruncher;
    connect(dt, SIGNAL(dataReady()), dc, SLOT(newEvent()));
    connect(dt, SIGNAL(bufferStatus(int)), mctrl->ui->bufferProgress, SLOT(setValue(int)));
    connect(dc, SIGNAL(bufferStatus(int)), mctrl->ui->bufferProgress, SLOT(setValue(int)));

    dc->setHistogram(m_histogram.value(0));
    dc->initRingbuffer(RINGBUFSIZE);

    dt->setRingbuffer(dc->getRingBuffer());

    dc->setRtData(rd);
    dc->setChannelSpectro(m_channelSpectro);

    m_readoutThread->start(QThread::TimeCriticalPriority);
    dc->start(QThread::NormalPriority);
#endif
}
#endif

Histogram *mvme::getHist(quint16 mod)
{
    Q_ASSERT(mod == 0);
    return m_histogram.value(0);
}

bool mvme::clearAllHist()
{
    m_histogram[0]->clearHistogram();
    return true;
}

Histogram *mvme::getHist()
{
    return m_histogram[0];
}

void mvme::closeEvent(QCloseEvent *event){
    qDebug("close Event");
    QSettings settings;
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());
    QMainWindow::closeEvent(event);
}


void mvme::on_actionSave_Histogram_triggered()
{
    auto subwin = ui->mdiArea->currentSubWindow();
    auto widget = subwin ? subwin->widget() : nullptr;
    auto tdw = qobject_cast<TwoDimWidget *>(widget);

    if (!tdw)
        return;

    quint32 channelIndex = tdw->getSelectedChannelIndex();

    QString buffer;
    buffer.sprintf("histogram_channel%02u.txt", channelIndex);
    QString fileName = QFileDialog::getSaveFileName(this, "Save Histogram", buffer,
                                                    "Text Files (*.txt);; All Files (*.*)");

    if (fileName.isEmpty())
        return;



    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
        return;

    QTextStream stream(&outFile);
    writeHistogram(stream, m_histogram[0], channelIndex);
}

void mvme::on_actionLoad_Histogram_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Load Histogram",
                                                   QString(),
                                                   "Text Files (*.txt);; All Files (*.*)");

    if (fileName.isEmpty())
        return;

    QFile inFile(fileName);
    if (!inFile.open(QIODevice::ReadOnly))
        return;

    QTextStream stream(&inFile);

    quint32 channelIndex = 0;

    readHistogram(stream, m_histogram[0], &channelIndex);

    auto subwin = ui->mdiArea->currentSubWindow();
    auto widget = subwin ? subwin->widget() : nullptr;
    auto tdw = qobject_cast<TwoDimWidget *>(widget);

    if (tdw)
    {
        tdw->setSelectedChannelIndex(channelIndex);
    }

    foreach(QMdiSubWindow *w, ui->mdiArea->subWindowList())
    {
        auto tdw = qobject_cast<TwoDimWidget *>(w->widget());
        if (tdw)
        {
            tdw->plot();
        }
    }
}

void mvme::on_actionExport_Histogram_triggered()
{
    auto subwin = ui->mdiArea->currentSubWindow();
    auto widget = subwin ? subwin->widget() : nullptr;
    auto tdw = qobject_cast<TwoDimWidget *>(widget);

    if (tdw)
    {
        tdw->exportPlot();
    }
}

void mvme::on_actionExport_Spectrogram_triggered()
{
    auto subwin = ui->mdiArea->currentSubWindow();
    auto widget = subwin ? subwin->widget() : nullptr;
    auto spectroWidget = qobject_cast<ChannelSpectroWidget *>(widget);

    if (spectroWidget)
    {
        spectroWidget->exportPlot();
    }
}

void mvme::on_actionNewConfig_triggered()
{
    if (m_context->getConfig()->isModified())
    {
        auto msgBox = new QMessageBox(QMessageBox::Question, "Configuration modified",
                                      "The current configuration has modifications. Do you want to save it?",
                                      QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);
        int result = msgBox->exec();

        if (result == QMessageBox::Save)
        {
            on_actionSaveConfig_triggered();
        }
        else if (result == QMessageBox::Cancel)
        {
            return;
        }
    }

    m_context->setConfig(new DAQConfig);
    m_context->setConfigFileName(QString());
}

void mvme::on_actionLoadConfig_triggered()
{
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

void mvme::on_actionSaveConfig_triggered()
{
    if (m_context->getConfigFileName().isEmpty())
    {
        on_actionSaveConfigAs_triggered();
        return;
    }

    QString fileName = m_context->getConfigFileName();
    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
    }

    if (outFile.write(m_context->getConfig()->toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return;
    }

    m_context->getConfig()->setModified(false);
}

void mvme::on_actionSaveConfigAs_triggered()
{
    QString path = QFileInfo(QSettings().value("Files/LastConfigFile").toString()).absolutePath();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Save Config As", path,
                                                    "MVME Config Files (*.mvmecfg);; All Files (*.*)");

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".mvmecfg";
    }

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return;
    }

    if (outFile.write(m_context->getConfig()->toJson()) < 0)
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return;
    }

    m_context->setConfigFileName(fileName);
    m_context->getConfig()->setModified(false);
}

void mvme::on_actionOpen_Listfile_triggered()
{
#if 0
    QString path = QFileInfo(QSettings().value("Files/LastListFile").toString()).absolutePath();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load Listfile",
                                                    path,
                                                    "MVME Listfiles (*.mvmelst);; All Files (*.*)");
    if (fileName.isEmpty())
        return;
#endif
}

void mvme::on_actionShowLogWindow_triggered()
{
    m_logViewSubwin->widget()->show();
    m_logViewSubwin->show();
    m_logViewSubwin->showNormal();
    m_logViewSubwin->raise();
}

void mvme::on_mdiArea_subWindowActivated(QMdiSubWindow *subwin)
{
    auto widget = subwin ? subwin->widget() : nullptr;
    auto tdw = qobject_cast<TwoDimWidget *>(widget);

    ui->actionExport_Histogram->setVisible(tdw);
    ui->actionLoad_Histogram->setVisible(tdw);
    ui->actionSave_Histogram->setVisible(tdw);

    auto spectroWidget = qobject_cast<ChannelSpectroWidget *>(widget);

    ui->actionExport_Spectrogram->setVisible(spectroWidget);
}

void mvme::handleEventConfigClicked(EventConfig *config)
{
}

void mvme::handleModuleConfigClicked(ModuleConfig *config)
{
    QMdiSubWindow *subwin = 0;
    for (auto win: ui->mdiArea->subWindowList())
    {
        auto widget = qobject_cast<ModuleConfigWidget *>(win->widget());
        if (widget && widget->getConfig() == config)
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

void mvme::handleModuleConfigDoubleClicked(ModuleConfig *config)
{
    for (auto win: ui->mdiArea->subWindowList())
    {
        auto widget = qobject_cast<ModuleConfigWidget *>(win->widget());
        if (widget && widget->getConfig() == config)
        {
            return;
        }
    }

    auto widget = new ModuleConfigWidget(m_context, config);
    auto subwin = new QMdiSubWindow;
    subwin->setAttribute(Qt::WA_DeleteOnClose);
    subwin->setWidget(widget);
    subwin->show();
    ui->mdiArea->addSubWindow(subwin);
    ui->mdiArea->setActiveSubWindow(subwin);
}

void mvme::handleDeleteEventConfig(EventConfig *event)
{
}

void mvme::handleDeleteModuleConfig(ModuleConfig *module)
{
}

void mvme::handleHistogramClicked(const QString &name, Histogram *histo)
{
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

void mvme::handleHistogramDoubleClicked(const QString &name, Histogram *histo)
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

void mvme::openHistogramView(Histogram *histo)
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

void mvme::appendToLog(const QString &s)
{
  auto str(QString("%1: %2")
      .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
      .arg(s));

  m_logView->append(str);
}

void mvme::handleLogViewContextMenu(const QPoint &pos)
{
  auto menu = std::unique_ptr<QMenu>(m_logView->createStandardContextMenu(pos));
  auto action = menu->addAction("Clear");
  connect(action, &QAction::triggered, m_logView, &QTextBrowser::clear);
  menu->exec(m_logView->mapToGlobal(pos));
}

void mvme::updateWindowTitle()
{
    QString filePath = m_context->getConfigFileName();
    QString fileName =  QFileInfo(filePath).fileName();
    QString title;
    if (!fileName.isEmpty())
    {
        title = (QString("%1 - mvme2").arg(fileName));
    }
    else
    {
        title = (QString("mvme2"));
    }

    if (m_context->getConfig()->isModified())
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
