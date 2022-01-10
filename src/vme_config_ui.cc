/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "vme_config_ui.h"

#include <algorithm>
#include <cmath>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QThread>
#include <mesytec-mvlc/mvlc_constants.h>

#include "analysis/analysis.h"
#include "data_filter_edit.h"
#include "qt-collapsible-section/Section.h"
#include "vme_config.h"
#include "vme_script.h"
#include "vme_config_ui_variable_editor.h"

using namespace mesytec;
using namespace vats;

//
// EventConfigDialog
//
struct EventConfigDialogPrivate
{
    QLineEdit *le_name;
    QComboBox *combo_condition;
    QStackedWidget *stack_options;
    QDialogButtonBox *buttonBox;

    QSpinBox *spin_irqLevel,
             *spin_irqVector,
             *spin_vmusbTimerFrequency;

    QComboBox *combo_mvlcTimerBase;
    QDoubleSpinBox *spin_timerPeriod;

    QCheckBox *cb_irqUseIACK;

    VariableEditorWidget *variableEditor;

    const VMEConfig *vmeConfig;

    void on_trigger_condition_changed()
    {
        auto tc = static_cast<TriggerCondition>(combo_condition->currentData().toInt());

        u8 irqValue = 0u;

        if (tc == TriggerCondition::Interrupt)
            irqValue = static_cast<u8>(spin_irqLevel->value());

        auto vars = variableEditor->getVariables();
        vars["sys_irq"].value = QString::number(irqValue);
        variableEditor->setVariables(vars);
    }

    void on_irq_level_changed()
    {
        u8 irqValue = static_cast<u8>(spin_irqLevel->value());
        variableEditor->setVariableValue("sys_irq", QString::number(irqValue));
    }
};

EventConfigDialog::EventConfigDialog(
    VMEController *controller,
    EventConfig *config,
    const VMEConfig *vmeConfig,
    QWidget *parent)
    : QDialog(parent)
    , m_d(new EventConfigDialogPrivate{})
    , m_controller(controller)
    , m_config(config)
{
    resize(700, 600);

    m_d->vmeConfig = vmeConfig;

    m_d->le_name = new QLineEdit;

    m_d->combo_condition = new QComboBox;
    m_d->stack_options = new QStackedWidget;
    m_d->buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    m_d->spin_irqLevel = new QSpinBox(this);
    m_d->spin_irqLevel->setMinimum(1);
    m_d->spin_irqLevel->setMaximum(16);

    m_d->spin_irqVector = new QSpinBox(this);
    m_d->spin_irqVector->setMaximum(255);

    m_d->cb_irqUseIACK = new QCheckBox(this);
    m_d->cb_irqUseIACK->hide();

    m_d->variableEditor = new VariableEditorWidget(this);

    auto gb_topOptions = new QGroupBox;
    {
        auto gb_layout   = new QFormLayout(gb_topOptions);
        gb_layout->setContentsMargins(2, 2, 2, 2);
        gb_layout->addRow(QSL("Name"), m_d->le_name);
        gb_layout->addRow(QSL("Condition"), m_d->combo_condition);
    }

    auto gb_variables = new QGroupBox("VME Script Variables");
    {
        auto layout = make_vbox<0, 0>(gb_variables);
        layout->addWidget(m_d->variableEditor);
    }

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(gb_topOptions);
    layout->addWidget(m_d->stack_options);
    layout->addWidget(gb_variables, 1);
    layout->addWidget(m_d->buttonBox);

    connect(m_d->combo_condition, qOverload<int>(&QComboBox::currentIndexChanged),
            m_d->stack_options, &QStackedWidget::setCurrentIndex);

    connect(m_d->combo_condition, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] (int /*index*/) { m_d->on_trigger_condition_changed(); });

    connect(m_d->spin_irqLevel, qOverload<int>(&QSpinBox::valueChanged),
            this, [this] () { m_d->on_irq_level_changed(); });

    connect(m_d->buttonBox, &QDialogButtonBox::accepted, this, &EventConfigDialog::accept);
    connect(m_d->buttonBox, &QDialogButtonBox::rejected, this, &EventConfigDialog::reject);

    // irq widget
    auto irqWidget = new QWidget(this);
    auto irqLayout = new QFormLayout(irqWidget);
    irqLayout->addRow(QSL("IRQ Level"), m_d->spin_irqLevel);
    irqLayout->addRow(QSL("IRQ Vector"), m_d->spin_irqVector);

    QVector<TriggerCondition> conditions;

    switch (controller->getType())
    {
        case VMEControllerType::VMUSB:
            {
                m_d->spin_timerPeriod = new QDoubleSpinBox;
                m_d->spin_timerPeriod->setPrefix(QSL("Every "));
                m_d->spin_timerPeriod->setSuffix(QSL(" seconds"));
                m_d->spin_timerPeriod->setMaximum(127.5);
                m_d->spin_timerPeriod->setDecimals(1);
                m_d->spin_timerPeriod->setSingleStep(0.5);

                m_d->spin_vmusbTimerFrequency = new QSpinBox;
                m_d->spin_vmusbTimerFrequency->setPrefix(QSL("Every "));
                m_d->spin_vmusbTimerFrequency->setSuffix(QSL(" events"));
                m_d->spin_vmusbTimerFrequency->setMaximum(65535);

                // vmusb timer widget
                auto vmusbTimerWidget = new QWidget;
                auto vmusbTimerLayout = new QFormLayout(vmusbTimerWidget);
                vmusbTimerLayout->addRow(QSL("Period"), m_d->spin_timerPeriod);
                vmusbTimerLayout->addRow(QSL("Frequency"), m_d->spin_vmusbTimerFrequency);

                conditions = { TriggerCondition::Interrupt,
                    TriggerCondition::NIM1, TriggerCondition::Periodic };

                m_d->stack_options->addWidget(irqWidget);
                m_d->stack_options->addWidget(new QWidget);         // nim has no special gui
                m_d->stack_options->addWidget(vmusbTimerWidget);
            } break;

        case VMEControllerType::SIS3153:
            {
                conditions = { TriggerCondition::Interrupt, TriggerCondition::Periodic,
                    TriggerCondition::Input1RisingEdge, TriggerCondition::Input1FallingEdge,
                    TriggerCondition::Input2RisingEdge, TriggerCondition::Input2FallingEdge
                };

                m_d->spin_timerPeriod = new QDoubleSpinBox;
                m_d->spin_timerPeriod->setPrefix(QSL("Every "));
                m_d->spin_timerPeriod->setSuffix(QSL(" seconds"));
                m_d->spin_timerPeriod->setMinimum(0.1);
                m_d->spin_timerPeriod->setMaximum(6.5);
                m_d->spin_timerPeriod->setDecimals(1);
                m_d->spin_timerPeriod->setSingleStep(0.1);
                m_d->spin_timerPeriod->setValue(1.0);

                auto timerWidget = new QWidget;
                auto timerLayout = new QFormLayout(timerWidget);
                timerLayout->addRow(QSL("Period"), m_d->spin_timerPeriod);

                m_d->stack_options->addWidget(irqWidget);
                m_d->stack_options->addWidget(timerWidget);
                for (int i=(int)TriggerCondition::Input1RisingEdge;
                     i<=(int)TriggerCondition::Input2FallingEdge;
                     ++i)
                {
                    m_d->stack_options->addWidget(new QWidget); // no special gui for external triggers
                }
            } break;

        case VMEControllerType::MVLC_USB:
        case VMEControllerType::MVLC_ETH:
            {
                // Hide the IRQ Vector line. The vector is not used by the MVLC.
                irqLayout->labelForField(m_d->spin_irqVector)->hide();
                m_d->spin_irqVector->hide();

                irqLayout->addRow(QSL("Use Interrupt Acknowledge (IRQUseIACK)"),
                                  m_d->cb_irqUseIACK);
                m_d->cb_irqUseIACK->show();

                auto label = new QLabel(
                    QSL("Note: enabling the IRQUseIACK option will make IRQ handling"
                        " slower but some VME modules might require it to work properly."));

                label->setWordWrap(true);
                irqLayout->addRow(label);
                m_d->stack_options->addWidget(irqWidget);

                // Timers
                {
                    m_d->combo_mvlcTimerBase = new QComboBox;
                    m_d->combo_mvlcTimerBase->addItem("ns", 0);
                    m_d->combo_mvlcTimerBase->addItem("us", 1);
                    m_d->combo_mvlcTimerBase->addItem("ms", 2);
                    m_d->combo_mvlcTimerBase->addItem("s", 3);

                    m_d->spin_timerPeriod = new QDoubleSpinBox;
                    m_d->spin_timerPeriod->setPrefix(QSL("Every "));
                    m_d->spin_timerPeriod->setDecimals(0);
                    m_d->spin_timerPeriod->setSingleStep(1.0);
                    m_d->spin_timerPeriod->setValue(1000);

                    auto on_timer_base_changed = [this] (const QString &unit)
                    {
                        m_d->spin_timerPeriod->setSuffix(QSL(" ") + unit);
                        m_d->spin_timerPeriod->setMinimum(
                            unit == "ns" ? mvlc::stacks::TimerPeriodMin_ns : 1);
                        m_d->spin_timerPeriod->setMaximum(mvlc::stacks::TimerPeriodMax);
                    };

                    on_timer_base_changed("ns");

                    connect(m_d->combo_mvlcTimerBase, qOverload<const QString &>(&QComboBox::currentIndexChanged),
                            this, on_timer_base_changed);

                    auto timerWidget = new QWidget;
                    auto timerLayout = new QFormLayout(timerWidget);
                    timerLayout->addRow(QSL("Timer Base"), m_d->combo_mvlcTimerBase);
                    timerLayout->addRow(QSL("Period"), m_d->spin_timerPeriod);
                    m_d->stack_options->addWidget(timerWidget);

                    // Trigger IO Condition

                    label = new QLabel(QSL(
                            "The event should be triggered via the MVLC Trigger I/O module.\n\n"
                            "Use the Trigger I/O Editor to setup one of the "
                            "StackStart units to trigger execution of this "
                            "events readout stack. Then connect the StackStart unit to the "
                            "desired activation signals."
                            ));
                    label->setWordWrap(true);
                    m_d->stack_options->addWidget(label);
                }

                conditions =
                {
                    TriggerCondition::Interrupt,
                    TriggerCondition::Periodic,
                    TriggerCondition::TriggerIO
                };
            } break;
    }

    for (auto cond: conditions)
    {
        m_d->combo_condition->addItem(TriggerConditionNames.value(cond, QSL("Unknown")),
                                      static_cast<s32>(cond));
    }

    loadFromConfig();
}

EventConfigDialog::~EventConfigDialog()
{
    delete m_d;
}

void EventConfigDialog::loadFromConfig()
{
    assert(m_d->vmeConfig);

    auto config = m_config;

    m_d->le_name->setText(config->objectName());

    int condIndex = m_d->combo_condition->findData(static_cast<s32>(config->triggerCondition));
    if (condIndex >= 0)
    {
        m_d->combo_condition->setCurrentIndex(condIndex);
    }

    m_d->spin_irqLevel->setValue(config->irqLevel);
    m_d->spin_irqVector->setValue(config->irqVector);
    m_d->cb_irqUseIACK->setChecked(config->triggerOptions["IRQUseIACK"].toBool());
    m_d->variableEditor->setVariables(config->getVariables());

    switch (m_controller->getType())
    {
        case VMEControllerType::VMUSB:
            {
                m_d->spin_timerPeriod->setValue(config->scalerReadoutPeriod * 0.5);
                m_d->spin_vmusbTimerFrequency->setValue(config->scalerReadoutFrequency);
            } break;
        case VMEControllerType::SIS3153:
            {
                m_d->spin_timerPeriod->setValue(
                    config->triggerOptions.value(QSL("sis3153.timer_period"), 0.0).toDouble());

            } break;

        case VMEControllerType::MVLC_USB:
        case VMEControllerType::MVLC_ETH:
            {
                m_d->combo_mvlcTimerBase->setCurrentText(
                    config->triggerOptions.value(QSL("mvlc.timer_base"), "ms").toString());

                m_d->spin_timerPeriod->setValue(
                    config->triggerOptions.value(QSL("mvlc.timer_period"), 1000u).toUInt());
            } break;
    }
}

void EventConfigDialog::saveToConfig()
{
    auto config = m_config;

    config->setObjectName(m_d->le_name->text());
    config->triggerCondition = static_cast<TriggerCondition>(m_d->combo_condition->currentData().toInt());
    config->irqLevel = static_cast<uint8_t>(m_d->spin_irqLevel->value());
    config->irqVector = static_cast<uint8_t>(m_d->spin_irqVector->value());
    config->setVariables(m_d->variableEditor->getVariables());

    switch (m_controller->getType())
    {
        case VMEControllerType::VMUSB:
            {
                config->scalerReadoutPeriod = static_cast<uint8_t>(m_d->spin_timerPeriod->value() * 2.0);
                config->scalerReadoutFrequency = static_cast<uint16_t>(m_d->spin_vmusbTimerFrequency->value());
            } break;

        case VMEControllerType::SIS3153:
            {
                config->triggerOptions[QSL("sis3153.timer_period")] = m_d->spin_timerPeriod->value();
            } break;

        case VMEControllerType::MVLC_USB:
        case VMEControllerType::MVLC_ETH:
            config->triggerOptions["IRQUseIACK"] = m_d->cb_irqUseIACK->isChecked();
            config->triggerOptions["mvlc.timer_base"] = m_d->combo_mvlcTimerBase->currentText();
            config->triggerOptions["mvlc.timer_period"] = m_d->spin_timerPeriod->value();
            break;
    }
    config->setModified(true);
}

void EventConfigDialog::accept()
{
    saveToConfig();
    QDialog::accept();
}

void EventConfigDialog::setReadOnly(bool readOnly)
{
    m_d->le_name->setEnabled(!readOnly);
    m_d->combo_condition->setEnabled(!readOnly);
    m_d->stack_options->setEnabled(!readOnly);
    m_d->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!readOnly);
}

struct ModuleConfigDialog::Private
{
    VariableEditorWidget *variableEditor;
};

//
// ModuleConfigDialog
//
ModuleConfigDialog::ModuleConfigDialog(
    ModuleConfig *module,
    const EventConfig *parentEvent,
    const VMEConfig *vmeConfig,
    QWidget *parent)
: QDialog(parent)
, m_module(module)
, m_vmeConfig(vmeConfig)
, m_d(std::make_unique<Private>())
{
    assert(module);
    assert(parentEvent);
    assert(vmeConfig);

    resize(500, 400);

    m_d->variableEditor = new VariableEditorWidget;

    setWindowTitle(QSL("Module Config"));
    MVMETemplates templates = read_templates();
    m_moduleMetas = templates.moduleMetas;

    /* Sort by vendorName and then displayName, giving the vendorName "mesytec"
     * the highest priority. Moduls without a vendor name always have to the
     * lowest prio. */
    std::sort(m_moduleMetas.begin(), m_moduleMetas.end(),
          [](const VMEModuleMeta &a, const VMEModuleMeta &b) {

        // Prio if vendorName is present in only one of the metas.
        if (a.vendorName.isEmpty() && !b.vendorName.isEmpty())
            return false;

        if (!a.vendorName.isEmpty() && b.vendorName.isEmpty())
            return true;

        // Sort by displayname
        if (a.vendorName == b.vendorName)
            return a.displayName < b.displayName;

        // Prio to mesytec modules.
        if (a.vendorName == QSL("mesytec"))
            return true;

        if (b.vendorName == QSL("mesytec"))
            return false;

        return a.vendorName < b.vendorName;
    });

    typeCombo = new QComboBox;
    int typeComboIndex = -1;
    QString currentVendor;

    for (const auto &mm: m_moduleMetas)
    {
        if (currentVendor.isNull())
            currentVendor = mm.vendorName;

        if (mm.vendorName != currentVendor)
        {
            typeCombo->insertSeparator(typeCombo->count());
            currentVendor = mm.vendorName;
        }

        typeCombo->addItem(mm.displayName, mm.typeId);

        if (mm.typeId == module->getModuleMeta().typeId)
        {
            typeComboIndex = typeCombo->count() - 1;
        }
    }

    if (typeComboIndex < 0 && !m_moduleMetas.isEmpty())
    {
        typeComboIndex = 0;
    }

    nameEdit = new QLineEdit;

    addressEdit = new QLineEdit;
    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    addressEdit->setFont(font);
    addressEdit->setInputMask("\\0\\xHHHH\\0\\0\\0\\0");
    addressEdit->setText(QString("0x%1").arg(module->getBaseAddress(), 8, 16, QChar('0')));

    const bool isNewModule = (module->getModuleMeta().typeId
                              == vats::VMEModuleMeta::InvalidTypeId);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QFormLayout(this);

    if (!isNewModule)
    {
        typeCombo->setEnabled(false);
    }

    auto gb_variables = new QGroupBox("VME Script Variables");
    {
        auto layout = make_vbox<0, 0>(gb_variables);
        layout->addWidget(m_d->variableEditor);

        m_d->variableEditor->setVariables(m_module->getVariables());

        auto sizePol = gb_variables->sizePolicy();
        sizePol.setVerticalStretch(1);
        gb_variables->setSizePolicy(sizePol);
    }

    layout->addRow("Type", typeCombo);
    layout->addRow("Name", nameEdit);
    layout->addRow("Address", addressEdit);
    layout->addRow(gb_variables);
    layout->addRow(bb);

    auto onTypeComboIndexChanged = [this](int /*index*/)
    {
        u8 typeId = typeCombo->currentData().toUInt();

        auto it = std::find_if(
            m_moduleMetas.begin(), m_moduleMetas.end(),
            [typeId] (const auto &mm) { return mm.typeId == typeId; });

        assert(it != m_moduleMetas.end());

        if (it == m_moduleMetas.end()) return;

        const auto &mm(*it);
        QString name = m_module->objectName();

        if (name.isEmpty())
        {
            name = make_unique_module_name(mm.typeName, m_vmeConfig);
        }

        nameEdit->setText(name);

        if (mm.vmeAddress != 0u)
        {
            addressEdit->setText(QString("0x%1")
                                 .arg(mm.vmeAddress, 8, 16, QChar('0')));
        }
    };

    connect(typeCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            this, onTypeComboIndexChanged);

    if (typeComboIndex >= 0)
    {
        typeCombo->setCurrentIndex(typeComboIndex);
        onTypeComboIndexChanged(typeComboIndex);
    }

    auto updateOkButton = [=]()
    {
        bool isOk = (addressEdit->hasAcceptableInput() && typeCombo->count() > 0);
        bb->button(QDialogButtonBox::Ok)->setEnabled(isOk);
    };

    connect(addressEdit, &QLineEdit::textChanged, updateOkButton);
    updateOkButton();
}

ModuleConfigDialog::~ModuleConfigDialog()
{
}

void ModuleConfigDialog::accept()
{
    bool ok;
    u8 typeId = typeCombo->currentData().toUInt();

    auto it = std::find_if(
        m_moduleMetas.begin(), m_moduleMetas.end(),
        [typeId] (const auto &mm) { return mm.typeId == typeId; });

    assert(it != m_moduleMetas.end());

    if (it == m_moduleMetas.end()) return;

    const auto &mm(*it);
    m_module->setModuleMeta(mm);
    m_module->setObjectName(nameEdit->text());
    m_module->setBaseAddress(addressEdit->text().toUInt(&ok, 16));
    m_module->setVariables(m_d->variableEditor->getVariables());

    QDialog::accept();
}

#if 0
VHS4030pWidget::VHS4030pWidget(MVMEContext *context, ModuleConfig *config, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::VHS4030pWidget)
    , m_context(context)
    , m_config(config)
{
    ui->setupUi(this);

    connect(ui->pb_write, &QPushButton::clicked, this, [this] {
        bool ok;
        u32 offset = ui->le_offset->text().toUInt(&ok, 16);
        u32 value  = ui->le_value->text().toUInt(&ok, 16);
        auto ctrl = m_context->getController();

        ctrl->write16(m_config->baseAddress + offset, value, m_config->getRegisterAddressModifier());
    });

    connect(ui->pb_read, &QPushButton::clicked, this, [this] {
        bool ok;
        u32 offset = ui->le_offset->text().toUInt(&ok, 16);
        auto ctrl = m_context->getController();
        u16 value = 0;
        u32 address = m_config->baseAddress + offset;

        int result = ctrl->read16(address, &value, m_config->getRegisterAddressModifier());

        qDebug("read16: addr=%08x, value=%04x, result=%d", address, value, result);

        ui->le_value->setText(QString("0x%1")
                              .arg(static_cast<u32>(value), 4, 16, QLatin1Char('0')));
    });
}
#endif

