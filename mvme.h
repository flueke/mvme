#ifndef MVME_H
#define MVME_H

#include <QMainWindow>
#ifdef VME_CONTROLLER_WIENER
#include "vmusb.h"
#endif
#ifdef VME_CONTROLLER_CAEN
#include "caenusb.h"
#endif
#include <QFile>
#include <QTextStream>
#include <QMap>

class mvmeControl;
class Histogram;
class DataThread;
class DataCruncher;
class QwtPlotCurve;
class QTimer;
class vmedevice;
class VirtualMod;
class Diagnostics;
class RealtimeData;
class ChannelSpectro;
class QThread;
class QMdiSubWindow;


namespace Ui {
class mvme;
}

class mvme : public QMainWindow
{
    Q_OBJECT

public:
    explicit mvme(QWidget *parent = 0);
    ~mvme();
    void plot();
    void startDatataking(quint16 period, bool multi, quint16 readLen, bool mblt);
    void stopDatataking();
    void initThreads();
    Histogram * getHist(quint16 mod);
    bool loadSetup();
    bool findController();
    bool createHistograms();
    bool clearAllHist();
    Histogram* getHist();

    vmUsb* vu;
    //caenusb* cu;
    mvmeControl *mctrl;
    DataThread *dt;
    DataCruncher *dc;
    Diagnostics* diag;
    RealtimeData* rd;
    ChannelSpectro *m_channelSpectro;


    void closeEvent(QCloseEvent *event);

public slots:
    void replot();
    void drawTimerSlot();
    void displayAbout();
    void createNewHistogram();
    void createNewChannelSpectrogram();
    void cascade();
    void tile();

private slots:
    void on_actionSave_Histogram_triggered();
    void on_actionLoad_Histogram_triggered();
    void on_actionExport_Histogram_triggered();
    void on_mdiArea_subWindowActivated(QMdiSubWindow *);

private:
    Ui::mvme *ui;
    unsigned char displayCounter;
    QString str;
    bool listmode;
    QString listfilename;
    QFile datfile;
    QString debugfilename;
    QFile debugfile;
    bool datataking;
    bool blockContinue;
    QDataStream dataStream;
    QTextStream debugStream;
    QString s;
    bool partialEvent;
    QTimer* drawTimer;
    // list of possibly connected VME devices
    QMap<int, vmedevice *> m_vmeDev;
    QMap<int, Histogram *> m_histogram;
    QMap<int, VirtualMod *> m_virtualMod;

    QThread *m_readoutThread;
};

#endif // MVME_H
