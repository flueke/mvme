#include "mvme.h"
#include "ui_mvme.h"
#include "vmusb.h"
#include "mvmecontrol.h"
#include "ui_mvmecontrol.h" // FIXME
#include "mvme_context.h"
#include "vme_module.h"
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

#include <QDockWidget>
#include <QFileDialog>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QtGui>
#include <QTimer>
#include <QToolBar>
#include <QTextEdit>
#include <QFont>

#include <qwt_plot_curve.h>

EventConfigWidget::EventConfigWidget(EventConfig *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
}

QString *getConfigStringByListIndex(int index, ModuleConfig *config)
{
    QString *ret = 0;

    switch (index)
    {
        case 0:
            ret = &config->initParameters;
            break;
        case 1:
            ret = &config->initReadout;
            break;
        case 2:
            ret = &config->readoutStack;
            break;
        case 3:
            ret = &config->initStartDaq;
            break;
        case 4:
            ret = &config->initStopDaq;
            break;
        case 5:
            ret = &config->initReset;
            break;
    }

    return ret;
}

ModuleConfigWidget::ModuleConfigWidget(ModuleConfig *config, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ModuleConfigWidget)
    , m_config(config)
{
    ui->setupUi(this);
    ui->combo_listType->addItem("Module Init");
    ui->combo_listType->addItem("Readout Settings");
    ui->combo_listType->addItem("Readout Stack");
    ui->combo_listType->addItem("Start DAQ");
    ui->combo_listType->addItem("Stop DAQ");
    ui->combo_listType->addItem("Module Reset");

    connect(ui->combo_listType, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ModuleConfigWidget::handleListTypeChanged);

    ui->label_type->setText(VMEModuleTypeNames[config->type]);
    ui->le_name->setText(config->name);
    ui->le_address->setText(QString().sprintf("0x%08x", config->baseAddress));
    ui->le_mcstAddress->setText(QString().sprintf("0x%02x", config->mcstAddress >> 24));
    ui->editor->setPlainText(*getConfigStringByListIndex(0, m_config));
    ui->editor->document()->setModified(false);
}

void ModuleConfigWidget::handleListTypeChanged(int index)
{
    if (m_currentListTypeIndex >= 0 && ui->editor->document()->isModified())
    {
        QString *dest = getConfigStringByListIndex(m_currentListTypeIndex, m_config);
        if (dest)
        {
            *dest = ui->editor->toPlainText();
        }
    }

    m_currentListTypeIndex = index;

    ui->editor->clear();
    ui->editor->document()->clearUndoRedoStacks();
    QString *contents = getConfigStringByListIndex(index, m_config);
    if (contents)
    {
        ui->editor->setPlainText(*contents);
    }
    ui->editor->document()->setModified(false);
}

void ModuleConfigWidget::closeEvent(QCloseEvent *event)
{
    qDebug() << __PRETTY_FUNCTION__;
    QWidget::closeEvent(event);
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
    ui(new Ui::mvme),
    m_readoutThread(new QThread)
    , m_context(new MVMEContext)
{

    qDebug() << "main thread: " << QThread::currentThread();

    m_readoutThread->setObjectName("ReadoutThread Old");

    m_histogram[0] = new Histogram(this, 42, 8192);
    m_histogram[0]->initHistogram();

    m_channelSpectro->setXAxisChannel(0);
    m_channelSpectro->setYAxisChannel(1);

    // create and initialize displays
    ui->setupUi(this);
    auto contextWidget = new MVMEContextWidget(m_context);
    m_contextWidget = contextWidget;
    connect(contextWidget, &MVMEContextWidget::eventClicked, this, &mvme::handleEventConfigClicked);
    connect(contextWidget, &MVMEContextWidget::moduleClicked, this, &mvme::handleModuleConfigClicked);
    connect(contextWidget, &MVMEContextWidget::moduleDoubleClicked, this, &mvme::handleModuleConfigDoubleClicked);

    connect(contextWidget, &MVMEContextWidget::deleteEvent, this, &mvme::handleDeleteEventConfig);
    connect(contextWidget, &MVMEContextWidget::deleteModule, this, &mvme::handleDeleteModuleConfig);


    auto contextDock = new QDockWidget("Configuration");
    contextDock->setObjectName("MVMEContextDock");
    contextDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    contextDock->setWidget(contextWidget);
    addDockWidget(Qt::LeftDockWidgetArea, contextDock);

    createNewHistogram();
    createNewChannelSpectrogram();

    rd = new RealtimeData;
    diag = new Diagnostics;

    // check and initialize VME interface
    vu = new VMUSB;
    m_context->setController(vu);
    vu->getUsbDevices();
    vu->openFirstUsbDevice();

    mctrl = new mvmeControl(this);
    mctrl->show();

    // read current configuration
    mctrl->getValues();

    drawTimer = new QTimer(this);
    connect(drawTimer, SIGNAL(timeout()), SLOT(drawTimerSlot()));

    initThreads();

    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());


#if 0
    //
    // event 0
    //
    {
        auto module00 = new MDPP16(0x0, "mdpp16_0");
        module00->initListString = QString(
                "0x6010 1        # irq level\n"
                "0x6012 0        # irq vector\n"
                "0x6018 1        # FIFO threshold\n"
                "0x601A 1        # max transfer data for multi event mode 3\n"
                "0x6036 0        # multi event\n"
                ""
                "0x6050 0x3FF8 \n"
                "0x6054 0x10\n"
                "0x6058 0x100\n"
                "0x6100 0x8\n"
                "0x611A 500\n"
                "0x6110 4\n"
                "0x6124 200\n"
                "0x6070 3\n"
                "0x6072 1000\n"
                ""
                "0x603A 1        # start acquisition\n"
                "0x603C 1        # FIFO reset\n"
                "0x6034 1        # readout reset\n"
                );

        auto module01 = new MADC32(0x43210000, "madc32_0");
        module01->initListString = QString(
                "0x6070 7\n"
                );

        auto event0 = new DAQEventConfig;
        event0->name = "event0";
        event0->triggerCondition = TriggerCondition::Interrupt;
        event0->irqLevel = 1;
        event0->modules.push_back(module00);
        event0->modules.push_back(module01);

        //m_context->addEventConfig(event0);
    }

    //
    // event 1
    //
    {
        auto module10 = new MQDC32(0x00010000, "mqdc32_0");
        module10->initListString = QString(
                "0x6070 1\n"
                );

        auto module11 = new MTDC32(0x00020000, "mtdc32_0");
        module11->initListString = QString(
                "0x6010 2        # irq level\n"
                "0x6012 0xf      # irq vector\n"
                "0x6070 3\n"
                );

        auto event1 = new DAQEventConfig;
        event1->name = "event1";
        event1->triggerCondition = TriggerCondition::Interrupt;
        event1->irqLevel = 2;
        event1->irqVector = 0xf;
        event1->modules.push_back(module10);
        event1->modules.push_back(module11);

        m_context->addEventConfig(event1);
    }

    //
    // event2
    //
    {
        auto module20 = new GenericModule(0, "testmod");
        module20->startCommands.addWrite16(0x00006090, 0x09, 1);
        u32 base = 0x00020000;
        module20->readoutCommands.addRead16(base + 0x6092);
        module20->readoutCommands.addRead16(base + 0x6094);
        module20->readoutCommands.addMarker(BerrMarker);
        module20->readoutCommands.addMarker(EndOfModuleMarker);

        auto event2 = new DAQEventConfig;
        event2->name = "periodic";
        event2->triggerCondition = TriggerCondition::Scaler;
        event2->scalerReadoutFrequency = 1000; // every x events
        event2->scalerReadoutPeriod = 1; // or every n*0.5 seconds
        event2->modules.push_back(module20);

        m_context->addEventConfig(event2);
    }
#endif

    //auto textView = new QTextEdit;
    //textView->setReadOnly(true);
    //textView->setFont(QFont("MonoSpace"));
    //textView->document()->setMaximumBlockCount(10 * 1024 * 1024);
    //connect(m_context->m_dataProcessor, &DataProcessor::eventFormatted,
    //        textView, &QTextEdit::append);
    //connect(m_context, &MVMEContext::daqStateChanged, [=](DAQState state) {
    //    if (state == DAQState::Starting)
    //    {
    //        textView->clear();
    //    }
    //});
    //
    //auto subwin = new QMdiSubWindow(ui->mdiArea);
    //subwin->setWidget(textView);
    //subwin->show();
}

mvme::~mvme()
{
    m_readoutThread->quit();
    m_readoutThread->wait();
    delete m_readoutThread;

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
    auto tdw = new TwoDimWidget(this, m_histogram.value(0));
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

void mvme::cascade()
{
    qDebug("implement cascade");
}

void mvme::tile()
{
    qDebug("implement tile");
}

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

    drawTimer->start(750);
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
}

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

void mvme::on_actionLoadConfig_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Load MVME Config",
                                                   QString(),
                                                    "MVME Config Files (*.mvmecfg);; All Files (*.*)");
    if (fileName.isEmpty())
        return;

    QFile inFile(fileName);
    if (!inFile.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error reading from %1").arg(fileName));
        return;
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
    m_context->getConfig()->read(doc.object());
    m_context->m_configFilename = fileName;

    m_contextWidget->reloadConfig();
}

void mvme::on_actionSaveConfig_triggered()
{
    if (m_context->m_configFilename.isEmpty())
    {
        on_actionSaveConfigAs_triggered();
        return;
    }

    QFile outFile(m_context->m_configFilename);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(m_context->m_configFilename));
    }

    QJsonObject configObject;
    m_context->getConfig()->write(configObject);

    QJsonDocument doc(configObject);
    outFile.write(doc.toJson());
}

void mvme::on_actionSaveConfigAs_triggered()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Save Config As", QString(),
                                                    "MVME Config Files (*.mvmecfg);; All Files (*.*)");

    if (fileName.isEmpty())
        return;

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error writing to %1").arg(fileName));
        return;
    }

    QJsonObject configObject;
    m_context->getConfig()->write(configObject);

    QJsonDocument doc(configObject);
    outFile.write(doc.toJson());

    m_context->m_configFilename = fileName;
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
    QMdiSubWindow *subwin = 0;
    for (auto win: ui->mdiArea->subWindowList())
    {
        auto widget = qobject_cast<ModuleConfigWidget *>(win->widget());
        if (widget && widget->getConfig() == config)
        {
            subwin = win;
            return;
        }
    }

    auto widget = new ModuleConfigWidget(config);
    subwin = new QMdiSubWindow(ui->mdiArea);
    subwin->setAttribute(Qt::WA_DeleteOnClose);
    subwin->setWidget(widget);

    subwin->show();
    ui->mdiArea->setActiveSubWindow(subwin);
}

void mvme::handleDeleteEventConfig(EventConfig *event)
{
}

void mvme::handleDeleteModuleConfig(ModuleConfig *module)
{
}
