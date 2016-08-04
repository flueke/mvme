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
class EventConfig;
class ModuleConfig;
class MVMEContextWidget;

class QMdiSubWindow;
class QThread;
class QTimer;
class QwtPlotCurve;
class QTextBrowser;


namespace Ui {
class mvme;
class ModuleConfigWidget;
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

    void openHistogramView(Histogram *histo);

private slots:
    void on_actionSave_Histogram_triggered();
    void on_actionLoad_Histogram_triggered();
    void on_actionExport_Histogram_triggered();
    void on_actionExport_Spectrogram_triggered();
    void on_actionNewConfig_triggered();
    void on_actionLoadConfig_triggered();
    void on_actionSaveConfig_triggered();
    void on_actionSaveConfigAs_triggered();
    void on_actionShowLogWindow_triggered();

    void on_mdiArea_subWindowActivated(QMdiSubWindow *);

    void handleEventConfigClicked(EventConfig *event);
    void handleModuleConfigClicked(ModuleConfig *module);
    void handleModuleConfigDoubleClicked(ModuleConfig *module);

    void handleDeleteEventConfig(EventConfig *event);
    void handleDeleteModuleConfig(ModuleConfig *module);

    void handleHistogramClicked(const QString &name, Histogram *histo);
    void handleHistogramDoubleClicked(const QString &name, Histogram *histo);

private:
    Ui::mvme *ui;
    bool datataking;
    QTimer* drawTimer;

    // list of possibly connected VME devices
    //QMap<int, vmedevice *> m_vmeDev;
    QMap<int, Histogram *> m_histogram;
    //QMap<int, VirtualMod *> m_virtualMod;

    MVMEContext *m_context;
    MVMEContextWidget *m_contextWidget = 0;
    QTextBrowser *m_logView;
    QMdiSubWindow *m_logViewSubwin;
};

class EventConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        EventConfigWidget(EventConfig *config, QWidget *parent = 0);
        
    private:
        EventConfig *m_config;
};

class ModuleConfigWidget: public QWidget
{
    Q_OBJECT
    public:
        ModuleConfigWidget(MVMEContext *context, ModuleConfig *config, QWidget *parent = 0);
        ModuleConfig *getConfig() const { return m_config; }

    protected:
        virtual void closeEvent(QCloseEvent *event);

    private:
        void handleListTypeIndexChanged(int);
        void editorContentsChanged();
        void onNameEditFinished();
        void onAddressEditFinished();

        void loadFromFile();
        void loadFromTemplate();
        void saveToFile();
        void execList();

        Ui::ModuleConfigWidget *ui;
        QAction *actLoadTemplate, *actLoadFile;
        MVMEContext *m_context;
        ModuleConfig *m_config;
        int m_lastListTypeIndex = 0;
        bool m_ignoreEditorContentsChange = false;
};

#endif // MVME_H
