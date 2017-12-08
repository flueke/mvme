/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017 mesytec GmbH & Co. KG <info@mesytec.com>
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

class VMUSBSettingsWidget: public VMEControllerSettingsWidget
{
    public:
        VMUSBSettingsWidget(QWidget *parent = 0);

        virtual void validate() override {}
        virtual void loadSettings(const QVariantMap &settings) override;
        virtual QVariantMap getSettings() override;

    private:
        QCheckBox *m_cb_debugRawBuffers;
};

class SIS3153EthSettingsWidget: public VMEControllerSettingsWidget
{
    public:
        SIS3153EthSettingsWidget(QWidget *parent = 0);

        virtual void validate() override;
        virtual void loadSettings(const QVariantMap &settings) override;
        virtual QVariantMap getSettings() override;

    private:
        QLineEdit *m_le_sisAddress;
        QCheckBox *m_cb_jumboFrames;
        QCheckBox *m_cb_debugRawBuffers;
        QCheckBox *m_cb_disableBuffering;
        QCheckBox *m_cb_enableWatchdog;
};
#endif /* __VME_CONTROLLER_UI_P_H__ */
