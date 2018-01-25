/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
// VMUSBSettingsWidget
//
VMUSBSettingsWidget::VMUSBSettingsWidget(QWidget *parent)
    : VMEControllerSettingsWidget(parent)
    , m_cb_debugRawBuffers(new QCheckBox)
{
    auto l = new QFormLayout(this);
    l->addRow(QSL("Debug: Write raw buffer file"), m_cb_debugRawBuffers);
}

void VMUSBSettingsWidget::loadSettings(const QVariantMap &settings)
{
    m_cb_debugRawBuffers->setChecked(settings.value("DebugRawBuffers").toBool());
}

QVariantMap VMUSBSettingsWidget::getSettings()
{
    QVariantMap result;
    result["DebugRawBuffers"] = m_cb_debugRawBuffers->isChecked();
    return result;
}

//
// SIS3153EthSettingsWidget
//
SIS3153EthSettingsWidget::SIS3153EthSettingsWidget(QWidget *parent)
    : VMEControllerSettingsWidget(parent)
    , m_le_sisAddress(new QLineEdit)
    , m_cb_jumboFrames(new QCheckBox)
    , m_cb_debugRawBuffers(new QCheckBox)
    , m_cb_disableBuffering(new QCheckBox)
    , m_cb_disableWatchdog(new QCheckBox)
    , m_gb_enableForwarding(new QGroupBox)
    , m_le_forwardingAddress(new QLineEdit)
    , m_spin_forwardingPort(new QSpinBox)
{
    auto l = new QFormLayout(this);
    l->addRow(QSL("Hostname / IP Address"), m_le_sisAddress);
    l->addRow(QSL("Enable UDP Jumbo Frames"), m_cb_jumboFrames);
    l->addRow(QSL("Debug: Write raw buffer file"), m_cb_debugRawBuffers);
    l->addRow(QSL("Debug: Disable Buffering"), m_cb_disableBuffering);
    l->addRow(QSL("Debug: Disable Watchdog"), m_cb_disableWatchdog);

    m_gb_enableForwarding->setTitle("Enable UDP Forwarding");
    m_gb_enableForwarding->setCheckable(true);
    m_spin_forwardingPort->setMinimum(0);
    m_spin_forwardingPort->setMaximum(std::numeric_limits<u16>::max());

    auto forwardLayout = new QFormLayout(m_gb_enableForwarding);
    forwardLayout->addRow(QSL("Hostname / IP Address"), m_le_forwardingAddress);
    forwardLayout->addRow(QSL("UDP Port"), m_spin_forwardingPort);

    l->addRow(m_gb_enableForwarding);
}

void SIS3153EthSettingsWidget::validate()
{
    auto addressText = m_le_sisAddress->text();

    if (addressText.isEmpty())
    {
        throw QString(QSL("Hostname / IP Address is empty"));
    }
}

static const QString DefaultHostname("sis3153-0040");
static const u16 DefaultForwardingPort = 42101;

void SIS3153EthSettingsWidget::loadSettings(const QVariantMap &settings)
{
    m_cb_jumboFrames->setChecked(settings["JumboFrames"].toBool());
    m_cb_debugRawBuffers->setChecked(settings.value("DebugRawBuffers").toBool());
    m_cb_disableBuffering->setChecked(settings.value("DisableBuffering").toBool());
    m_cb_disableWatchdog->setChecked(settings.value("DisableWatchdog").toBool());
    m_gb_enableForwarding->setChecked(settings.value("UDP_Forwarding_Enable").toBool());

    // Use the hostname from the setting first.
    auto hostname = settings["hostname"].toString();

    if (hostname.isEmpty())
    {
        // Next check the application wide settings for a previously connected address.
        QSettings appSettings;
        hostname = appSettings.value("VME/LastConnectedSIS3153").toString();
    }

    if (hostname.isEmpty())
    {
        // Still no hostname. Use the default name.
        hostname = DefaultHostname;
    }

    m_le_sisAddress->setText(hostname);

    m_le_forwardingAddress->setText(settings.value("UDP_Forwarding_Address").toString());
    m_spin_forwardingPort->setValue(settings.value("UDP_Forwarding_Port", DefaultForwardingPort).toUInt());
}

QVariantMap SIS3153EthSettingsWidget::getSettings()
{
    QVariantMap result;
    result["hostname"] = m_le_sisAddress->text();
    result["JumboFrames"] = m_cb_jumboFrames->isChecked();
    result["DebugRawBuffers"] = m_cb_debugRawBuffers->isChecked();
    result["DisableBuffering"] = m_cb_disableBuffering->isChecked();
    result["DisableWatchdog"] = m_cb_disableWatchdog->isChecked();
    result["UDP_Forwarding_Enable"] = m_gb_enableForwarding->isChecked();
    result["UDP_Forwarding_Address"] = m_le_forwardingAddress->text();
    result["UDP_Forwarding_Port"] = static_cast<u16>(m_spin_forwardingPort->value());
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
        else
        {
            settingsWidget->loadSettings(QVariantMap());
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
    VMEControllerFactory f(selectedType);
    auto controller = f.makeController(settings);
    qDebug() << "before m_context->setVMEController()";
    m_context->setVMEController(controller, settings);
    qDebug() << "after m_context->setVMEController()";

    if (buttonRole == QDialogButtonBox::AcceptRole)
    {
        accept();
    }
}
