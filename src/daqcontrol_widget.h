/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
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
#ifndef __DAQCONTROL_WIDGET_H__
#define __DAQCONTROL_WIDGET_H__

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QWidget>

#include "globals.h"
#include "libmvme_export.h"
#include "mvme_stream_worker.h"

class QFormLayout;

namespace Ui
{
    class DaqRunSettingsDialog;
};

class LIBMVME_EXPORT DAQControlWidget: public QWidget
{
    Q_OBJECT
    signals:
        void startDAQ(u32 nCycles, bool keepHistoContents,
                      const std::chrono::milliseconds &runDuration);
        void pauseDAQ();
        void resumeDAQ(u32 nCycles);
        void stopDAQ();
        void reconnectVMEController();
        void forceResetVMEController();
        void listFileOutputInfoModified(const ListFileOutputInfo &lfo);
        // Signalling that the user wants to change the specific settings.
        void changeVMEControllerSettings();
        void changeDAQRunSettings();
        void changeWorkspaceSettings();
        void showRunNotes();

    public:
        explicit DAQControlWidget(QWidget *parent = 0);
        ~DAQControlWidget();

    public slots:
        void setGlobalMode(const GlobalMode &mode);
        void setDAQState(const DAQState &state);
        void setVMEControllerState(const ControllerState &state);
        void setVMEControllerTypeName(const QString &name);
        void setStreamWorkerState(const AnalysisWorkerState &state);
        void setListFileOutputInfo(const ListFileOutputInfo &info);
        void setDAQStats(const DAQStats &stats);
        void setWorkspaceDirectory(const QString &dir);
        void setMVMEState(const MVMEState &state);

        // Call this periodcally after updating the other variables.
        void updateWidget();

    private:

        GlobalMode m_globalMode = GlobalMode::DAQ;
        DAQState m_daqState = DAQState::Idle;
        ControllerState m_vmeControllerState = ControllerState::Disconnected;
        QString m_vmeControllerTypeName;
        AnalysisWorkerState m_streamWorkerState = AnalysisWorkerState::Idle;
        ListFileOutputInfo m_listFileOutputInfo;
        DAQStats m_daqStats = {};
        QString m_workspaceDirectory;
        MVMEState m_mvmeState = MVMEState::Idle;

        QPushButton *pb_start,
                    *pb_stop,
                    *pb_oneCycle,
                    *pb_reconnect,
                    *pb_controllerSettings,
                    *pb_runSettings,
                    *pb_workspaceSettings,
                    *pb_runNotes,
                    *pb_forceReset;

        QLabel *label_controllerState,
               *label_daqState,
               *label_analysisState,
               *label_listfileSize,
               *label_freeStorageSpace;

        QCheckBox *cb_writeListfile;

        QComboBox *combo_compression;

        QLineEdit *le_listfileFilename;

        QGroupBox *gb_listfile;
        QFormLayout *gb_listfileLayout;

        QRadioButton *rb_keepData, *rb_clearData;
        QButtonGroup *bg_daqData;
        QSpinBox *spin_runDuration;
};

class LIBMVME_EXPORT DAQRunSettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        DAQRunSettingsDialog(const ListFileOutputInfo &settings, QWidget *parent = 0);
        virtual ~DAQRunSettingsDialog();

        ListFileOutputInfo getSettings() const { return m_settings; }

        void accept() override;

    private:
        void updateSettings();
        void updateExample();

        Ui::DaqRunSettingsDialog *ui;
        ListFileOutputInfo m_settings;
};

class LIBMVME_EXPORT WorkspaceSettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        WorkspaceSettingsDialog(const std::shared_ptr<QSettings> &settings, QWidget *parent = 0);

    public slots:
        virtual void accept() override;
        virtual void reject() override;

    private slots:
        void selectListfileDir();

    private:
        void populate();

        QGroupBox *gb_jsonRPC,
                  *gb_eventServer;

        QLineEdit *le_jsonRPCListenAddress,
                  *le_eventServerListenAddress,
                  *le_expName,
                  *le_expTitle,
                  *le_listfileDir;

        QPushButton *pb_listfileDir;

        QSpinBox *spin_jsonRPCListenPort,
                 *spin_eventServerListenPort;

        QCheckBox *cb_ignoreStartupErrors;

        QDialogButtonBox *m_bb;

        std::shared_ptr<QSettings> m_settings;
};

#endif /* __DAQCONTROL_WIDGET_H__ */
