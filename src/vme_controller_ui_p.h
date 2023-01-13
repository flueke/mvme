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
#ifndef __VME_CONTROLLER_UI_P_H__
#define __VME_CONTROLLER_UI_P_H__

#include "vme_controller_ui.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QTextBrowser>

class VMUSBSettingsWidget: public VMEControllerSettingsWidget
{
    public:
        explicit VMUSBSettingsWidget(QWidget *parent = 0);

        virtual void validate() override {}
        virtual void loadSettings(const QVariantMap &settings) override;
        virtual QVariantMap getSettings() override;

    private:
        QCheckBox *m_cb_debugRawBuffers;
};

class SIS3153EthSettingsWidget: public VMEControllerSettingsWidget
{
    public:
        explicit SIS3153EthSettingsWidget(QWidget *parent = 0);

        virtual void validate() override;
        virtual void loadSettings(const QVariantMap &settings) override;
        virtual QVariantMap getSettings() override;

    private:
        QLineEdit *m_le_sisAddress;
        QCheckBox *m_cb_jumboFrames;
        QCheckBox *m_cb_debugRawBuffers;
        QCheckBox *m_cb_disableBuffering;
        QCheckBox *m_cb_disableWatchdog;
        QGroupBox *m_gb_enableForwarding;
        QLineEdit *m_le_forwardingAddress;
        QSpinBox *m_spin_forwardingPort;
        QComboBox *m_combo_packetGap;
};

class MVLC_USB_SettingsWidget: public VMEControllerSettingsWidget
{
    public:
        explicit MVLC_USB_SettingsWidget(QWidget *parent = nullptr);

        virtual void validate() override;
        virtual void loadSettings(const QVariantMap &settings) override;
        virtual QVariantMap getSettings() override;

    private slots:
        void listDevices();

    private:
        QRadioButton *rb_first,
                     *rb_index,
                     *rb_serial;

        QSpinBox *spin_index;
        QLineEdit *le_serial;
        QPushButton *pb_listDevices;
        QTextBrowser *tb_devices;
};

class MVLC_ETH_SettingsWidget: public VMEControllerSettingsWidget
{
    public:
        explicit MVLC_ETH_SettingsWidget(QWidget *parent = nullptr);

        virtual void validate() override;
        virtual void loadSettings(const QVariantMap &settings) override;
        virtual QVariantMap getSettings() override;

    private:
        QLineEdit *le_address;
        QCheckBox *cb_jumboFrames;
};


#endif /* __VME_CONTROLLER_UI_P_H__ */
