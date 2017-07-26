/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef MVME_H
#define MVME_H

#include <QMainWindow>
#include <QMap>

#include "libmvme_export.h"

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
class QNetworkAccessManager;


namespace Ui {
class mvme;
}

class LIBMVME_EXPORT mvme : public QMainWindow
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
    void on_actionCheck_for_updates_triggered();

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
    QNetworkAccessManager *m_networkAccessManager = nullptr;
};

#endif // MVME_H
