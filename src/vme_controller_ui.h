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
#ifndef __VME_CONTROLLER_UI_H__
#define __VME_CONTROLLER_UI_H__

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QStackedWidget>

class MVMEContext;

class VMEControllerSettingsWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit VMEControllerSettingsWidget(QWidget *parent = 0)
            : QWidget(parent)
        { }

        virtual void validate() = 0; // must throw std::exception or qstring on error
        virtual void loadSettings(const QVariantMap &settings) = 0;
        virtual QVariantMap getSettings() = 0;
};

class VMEControllerSettingsDialog: public QDialog
{
    Q_OBJECT
    public:
        VMEControllerSettingsDialog(MVMEContext *context, QWidget *parent = 0);

    private:
        void onButtonBoxClicked(QAbstractButton *button);

        MVMEContext *m_context;
        QDialogButtonBox *m_buttonBox;
        QComboBox *m_comboType;
        QStackedWidget *m_controllerStack;
        QVector<VMEControllerSettingsWidget *> m_settingsWidgets;
};

#endif /* __VME_CONTROLLER_UI_H__ */
