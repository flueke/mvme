#ifndef MVME_H
#define MVME_H

#include <QMainWindow>
#include <QMap>

class ConfigObject;
class VMEConfig;
class VMEConfigTreeWidget;
class DAQControlWidget;
enum class DAQState;
class DAQStatsWidget;
class DataThread;
class Diagnostics;
class EventConfig;
class Hist2D;
class HistogramCollection;
class HistogramTreeWidget;
class ModuleConfig;
class MVMEContext;
class MVMEContextWidget;
class RealtimeData;
class VirtualMod;
class VMEDebugWidget;
class vmedevice;
class VMEScriptConfig;
class WidgetGeometrySaver;

class QMdiSubWindow;
class QPlainTextEdit;
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

    virtual void closeEvent(QCloseEvent *event);
    void restoreSettings();

    MVMEContext *getContext() { return m_context; }

    void addObjectWidget(QWidget *widget, QObject *object, const QString &stateKey);
    bool hasObjectWidget(QObject *object) const;
    QWidget *getObjectWidget(QObject *object) const;
    QList<QWidget *> getObjectWidgets(QObject *object) const;
    void activateObjectWidget(QObject *object);

    void addWidget(QWidget *widget, const QString &stateKey = QString());

public slots:
    void displayAbout();
    void displayAboutQt();
    void clearLog();

    void on_actionNewVMEConfig_triggered();
    void on_actionOpenVMEConfig_triggered();
    bool on_actionSaveVMEConfig_triggered();
    bool on_actionSaveVMEConfigAs_triggered();

    void loadConfig(const QString &fileName);

    void on_actionNewWorkspace_triggered();
    void on_actionOpenWorkspace_triggered();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_actionOpenListfile_triggered();
    void on_actionCloseListfile_triggered();

    void on_actionMainWindow_triggered();
    void on_actionAnalysis_UI_triggered();
    void on_actionVME_Debug_triggered();
    void on_actionLog_Window_triggered();
    void on_actionVMUSB_Firmware_Update_triggered();
    void on_actionTemplate_Info_triggered();

    void onObjectAboutToBeRemoved(QObject *obj);

    void appendToLog(const QString &);
    void updateWindowTitle();
    void onConfigChanged(VMEConfig *config);

    void onDAQAboutToStart(quint32 nCycles);
    void onDAQStateChanged(const DAQState &);

    void onShowDiagnostics(ModuleConfig *config);
    void on_actionImport_Histo1D_triggered();

    void on_actionVMEScriptRef_triggered();

    void updateActions();


private:
    bool createNewOrOpenExistingWorkspace();

    Ui::mvme *ui;

    MVMEContext *m_context;
    QPlainTextEdit *m_logView = nullptr;
    DAQControlWidget *m_daqControlWidget = nullptr;
    VMEConfigTreeWidget *m_vmeConfigTreeWidget = nullptr;
    DAQStatsWidget *m_daqStatsWidget = nullptr;
    VMEDebugWidget *m_vmeDebugWidget = nullptr;

    QMap<QObject *, QList<QWidget *>> m_objectWindows;

    WidgetGeometrySaver *m_geometrySaver;
    bool m_quitting = false;
};

#endif // MVME_H
