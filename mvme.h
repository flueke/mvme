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

class ChannelSpectro;
class DataCruncher;
class DataThread;
class Diagnostics;
class Histogram;
class MVMEContext;
class mvmeControl;
class RealtimeData;
class VirtualMod;
class vmedevice;

class QMdiSubWindow;
class QThread;
class QTimer;
class QwtPlotCurve;


namespace Ui {
class mvme;
}

class mvme : public QMainWindow
{
    Q_OBJECT

public:
    explicit mvme(QWidget *parent = 0);
    ~mvme();
    void startDatataking(quint16 period, bool multi, quint16 readLen, bool mblt, bool daqMode);
    void stopDatataking();
    void initThreads();
    Histogram * getHist(quint16 mod);
    bool clearAllHist();
    Histogram* getHist();

    VMUSB* vu;
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
    void on_actionExport_Spectrogram_triggered();
    void on_mdiArea_subWindowActivated(QMdiSubWindow *);

private:
    Ui::mvme *ui;
    bool datataking;
    QTimer* drawTimer;

    // list of possibly connected VME devices
    //QMap<int, vmedevice *> m_vmeDev;
    QMap<int, Histogram *> m_histogram;
    //QMap<int, VirtualMod *> m_virtualMod;

    QThread *m_readoutThread;
    MVMEContext *m_context;
};

#endif // MVME_H
