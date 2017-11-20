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
#include <QSpinBox>
#include <QWidget>

#include "globals.h"

class MVMEContext;

class DAQControlWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQControlWidget(MVMEContext *context, QWidget *parent = 0);
        ~DAQControlWidget();

    private:
        void updateWidget();

        MVMEContext *m_context;

        QPushButton *pb_start,
                    *pb_stop,
                    *pb_oneCycle,
                    *pb_reconnect,
                    *pb_controllerSettings,
                    *pb_runSettings;

        QLabel *label_controllerState,
               *label_daqState,
               *label_analysisState,
               *label_listfileSize;

        QCheckBox *cb_writeListfile;

        QComboBox *combo_compression;

        QLineEdit *le_listfileFilename;

        QGroupBox *gb_listfile;

        QRadioButton *rb_keepData, *rb_clearData;
        QButtonGroup *bg_daqData;
};

class DAQRunSettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        DAQRunSettingsDialog(const ListFileOutputInfo &settings, QWidget *parent = 0);

        ListFileOutputInfo getSettings() const { return m_settings; }

    private:
        void updateExample();

        ListFileOutputInfo m_settings;
        QLineEdit *le_prefix;
        QSpinBox *spin_runNumber;
        QCheckBox *cb_useRunNumber;
        QCheckBox *cb_useTimestamp;
        QLineEdit *le_exampleName;
        QDialogButtonBox *m_bb;
};

#endif /* __DAQCONTROL_WIDGET_H__ */
