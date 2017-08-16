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

#include "vme_controller_ui.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>

#include "qt_util.h"
#include "mvme_context.h"
#include "vme_controller_factory.h"
#include "vme_controller_ui_p.h"

//
// SIS3153EthSettingsWidget
//
SIS3153EthSettingsWidget::SIS3153EthSettingsWidget(QWidget *parent)
    : VMEControllerSettingsWidget(parent)
    , m_le_sisAddress(new QLineEdit)
{
    auto l  = new QFormLayout(this);
    l->addRow(QSL("Hostname / IP Address"), m_le_sisAddress);
}

void SIS3153EthSettingsWidget::validate()
{
    // TODO: resolve hostname, throw on error
    auto addressText = m_le_sisAddress->text();

    if (addressText.isEmpty())
    {
        throw QString(QSL("Hostname / IP Address is empty"));
    }
}

void SIS3153EthSettingsWidget::loadSettings(const QVariantMap &settings)
{
    m_le_sisAddress->setText(settings["hostname"].toString());
}

QVariantMap SIS3153EthSettingsWidget::getSettings()
{
    QVariantMap result;
    result["hostname"] = m_le_sisAddress->text();
    return result;
}


//
// VMEControllerSettingsDialog
//

namespace
{
    struct LabelAndType
    {
        const char *label;
        VMEControllerType type;
    };

    static const QVector<LabelAndType> LabelsAndTypes =
    {
        { "VM-USB",         VMEControllerType::VMUSB },
        { "SIS3153",        VMEControllerType::SIS3153 }
    };
}

VMEControllerSettingsDialog::VMEControllerSettingsDialog(MVMEContext *context, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
    , m_buttonBox(new QDialogButtonBox)
    , m_comboType(new QComboBox)
    , m_controllerStack(new QStackedWidget)
{
    setWindowTitle(QSL("VME Controller Selection"));

    auto widgetLayout = new QVBoxLayout(this);

    // add type combo groupbox
    {
        auto gb = new QGroupBox(QSL("Controller Type"));
        auto l  = new QHBoxLayout(gb);
        l->addWidget(m_comboType);
        widgetLayout->addWidget(gb);
    }

    auto currentControllerType = m_context->getVMEConfig()->getControllerType();
    s32 currentControllerIndex = 0;

    // fill combo and add settings widgets
    for (s32 i = 0; i< LabelsAndTypes.size(); ++i)
    {
        auto lt = LabelsAndTypes[i];
        m_comboType->addItem(lt.label, static_cast<s32>(lt.type));
        VMEControllerFactory f(lt.type);
        auto settingsWidget = f.makeSettingsWidget();

        if (lt.type == currentControllerType)
        {
            settingsWidget->loadSettings(m_context->getVMEConfig()->getControllerSettings());
            currentControllerIndex = i;
        }

        auto gb = new QGroupBox(QSL("Controller Settings"));
        auto l  = new QHBoxLayout(gb);
        l->setContentsMargins(2, 2, 2, 2);
        l->setSpacing(2);
        l->addWidget(settingsWidget);
        m_controllerStack->addWidget(gb);
        m_settingsWidgets.push_back(settingsWidget);
    }

    // controller config stack
    widgetLayout->addWidget(m_controllerStack);

    // buttonbox
    m_buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(m_buttonBox, &QDialogButtonBox::clicked, this, &VMEControllerSettingsDialog::onButtonBoxClicked);

    widgetLayout->addWidget(m_buttonBox);

    // setup
    connect(m_comboType, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            m_controllerStack, &QStackedWidget::setCurrentIndex);

    m_comboType->setCurrentIndex(currentControllerIndex);
}

void VMEControllerSettingsDialog::onButtonBoxClicked(QAbstractButton *button)
{
    auto buttonRole = m_buttonBox->buttonRole(button);

    if (buttonRole == QDialogButtonBox::RejectRole)
    {
        reject();
        return;
    }

    Q_ASSERT(buttonRole == QDialogButtonBox::AcceptRole || buttonRole == QDialogButtonBox::ApplyRole);

    // change controller type here
    // delete old controller
    // set new controller
    auto selectedType = static_cast<VMEControllerType>(m_comboType->currentData().toInt());
    VMEControllerFactory f(selectedType);

    auto settingsWidget = qobject_cast<VMEControllerSettingsWidget *>(
        m_settingsWidgets.value(m_comboType->currentIndex()));
    Q_ASSERT(settingsWidget);

    try
    {
        settingsWidget->validate();
    }
    catch (const QString &e)
    {
        QMessageBox::critical(this, QSL("Invalid Settings"),
                              QString("Settings validation failed: %1").arg(e));
        return;
    }

    auto settings = settingsWidget->getSettings();
    auto controller = f.makeController(settings);
    m_context->setVMEController(controller, settings);

    if (buttonRole == QDialogButtonBox::AcceptRole)
    {
        accept();
    }
}
