#include "mvme.h"
#include "ui_mvme.h"
#include "vmusb.h"
#include "mvmecontrol.h"
#include "ui_mvmecontrol.h"
#include <qwt_plot_curve.h>
#include "histogram.h"
#include "datacruncher.h"
#include "datathread.h"
#include "mvmedefines.h"
#include <QTimer>
#include <QtGui>
#include <QMessageBox>
#include "twodimdisp.h"
#include "diagnostics.h"
#include "realtimedata.h"
#include "channelspectro.h"


mvme::mvme(QWidget *parent) :
    QMainWindow(parent),
    vu(0),
    mctrl(0),
    dt(0),
    dc(0),
    diag(0),
    rd(0),
    m_channelSpectro(new ChannelSpectro),
    ui(new Ui::mvme),
    m_readoutThread(new QThread)
{

    qDebug() << "main thread: " << QThread::currentThread();

    m_readoutThread->setObjectName("ReadoutThread");

    // TODO: currently not fully implemented
    loadSetup();

    // TODO: not fully implemented yet
    findController();

    // TODO: not fully implemented yet
    createHistograms();
    m_histogram[0] = new Histogram(this, 42, 8192);
    m_histogram[0]->initHistogram();

    m_channelSpectro->setXAxisChannel(0);
    m_channelSpectro->setYAxisChannel(1);

    // create and initialize displays
    ui->setupUi(this);
    createChild();

    rd = new RealtimeData;
    diag = new Diagnostics;

    // check and initialize VME interface
    vu = new vmUsb;
    vu->getUsbDevices();
    if (!vu->openUsbDevice()) {
      qDebug("No VM USB controller found");
      return;
    }

    mctrl = new mvmeControl(this);
    mctrl->show();

    // read current configuration
    //vu->readAllRegisters();
    mctrl->getValues();

    //cu = new caenusb();
    //cu->openUsbDevice();
    //cu->openUsbDevice();

    // set default values
    //cu->initialize();


    drawTimer = new QTimer(this);
    connect(drawTimer, SIGNAL(timeout()), SLOT(drawTimerSlot()));

    initThreads();

    plot();

    QSettings settings("mesytec", "mvme");
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());

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
}

void mvme::plot()
{
    /*
    curve->setRawSamples((const double*)hist->m_axisBase, (const double*)hist->m_data, 8192);
    curve->setStyle(QwtPlotCurve::Steps);
    curve->attach(ui->mainPlot);
    ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, 8192);
//    ui->mainPlot->setAxisScale( QwtPlot::yLeft, -200.0, 200.0 );
    ui->mainPlot->replot();
*/
}

void mvme::replot()
{
    TwoDimDisp* tdd;
    /*
    quint8 chan, mod;
    chan = ui->channelBox->value();
    mod = ui->modBox->value();
    curve->setRawSamples((const double*)hist->m_axisBase, (const double*)&hist->m_data[32*mod+8192*chan], 8192);
    ui->mainPlot->replot();
*/
    foreach(QMdiSubWindow *w, ui->mdiArea->subWindowList()){
        tdd = qobject_cast<TwoDimDisp *>(w);
        if (tdd)
        {
            tdd->plot();
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

void mvme::createChild()
{
    TwoDimDisp* childDisplay = new TwoDimDisp(ui->mdiArea);
    childDisplay->setAttribute(Qt::WA_DeleteOnClose);
    childDisplay->show();
    childDisplay->setMvme(this);
    childDisplay->setHistogram(m_histogram.value(0));


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

void mvme::startDatataking(quint16 period, bool multi, quint16 readLen, bool mblt)
{
    qDebug() << __PRETTY_FUNCTION__;

    dt->setReadoutmode(multi, readLen, mblt);
    dt->startReading(period);

    drawTimer->start(750);
    datataking = true;
    // check for listfile
/*    if(listmode){
        datfile.setName(listfilename);
    // listfile already existing? Warning!
        if(QFile::exists(datfile.name())){
            int answer = QMessageBox::warning(
                    this, "Listfile Exists -- Overwrite File",
            QString("Overwrite existing listfile?"),
            "&Yes", "&No", QString::null, 1, 1 );
            if(answer){
                qDebug("abbruch");
                return;
            }

        }
        datfile.open(IO_WriteOnly);
        dataStream.setDevice(&datfile);
    }
    debugfile.setName(debugfilename);
    if(!debugfile.open(IO_ReadWrite))
        qDebug("error opening debugfile");
    debugStream.setDevice(&debugfile);
    datataking = true;

    eventsToRead = 0;
    evWordsToRead = 0;
    mEvWordsToRead = 0;
    bufferCounter = 0;
    daqTimer->start(period);
    startDisplaying();
*/
}

void mvme::stopDatataking()
{
    QTime timer;
    timer.start();

    dt->stopReading();
    drawTimer->stop();
    datataking = false;



    qDebug() << __PRETTY_FUNCTION__ << "elapsed:" << timer.elapsed();




/*    daqTimer->stop();
    if(listmode)
        datfile.close();
    debugfile.close();
    stopDisplaying();
*/
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

    dt->setRingbuffer(dc->m_pRingBuffer);

    dc->setRtData(rd);
    dc->setChannelSpectro(m_channelSpectro);
    plot();

    m_readoutThread->start(QThread::TimeCriticalPriority);
    dc->start(QThread::NormalPriority);
}

Histogram *mvme::getHist(quint16 mod)
{
    return m_histogram.value(0);
}

bool mvme::loadSetup()
{
    return true;
}

bool mvme::findController()
{
    return true;
}

bool mvme::createHistograms()
{
    return true;
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
    QSettings settings("mesytec", "mvme");
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());
    QMainWindow::closeEvent(event);
}
