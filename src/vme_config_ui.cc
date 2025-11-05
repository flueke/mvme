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
#include "vme_config_ui.h"

#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
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
#include <algorithm>
#include <cmath>
#include <mesytec-mvlc/mvlc_constants.h>

#include "analysis/analysis.h"
#include "data_filter_edit.h"
#include "mvlc/mvlc_trigger_io.h"
#include "qt-collapsible-section/Section.h"
#include "util/qt_font.h"
#include "vme_config.h"
#include "vme_config_ui_variable_editor.h"
#include "vme_config_util.h"
#include "vme_script.h"

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

    QSpinBox *spin_irqLevel, *spin_irqVector, *spin_vmusbTimerFrequency;

    QDoubleSpinBox *spin_timerPeriod;
    QSpinBox *spin_stackTimerPeriod;
    QSpinBox *spin_mvlcSlaveTriggerIndex;

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

EventConfigDialog::EventConfigDialog(VMEControllerType vmeControllerType, EventConfig *config,
                                     const VMEConfig *vmeConfig, QWidget *parent)
    : QDialog(parent)
    , m_d(new EventConfigDialogPrivate{})
    , m_vmeControllerType(vmeControllerType)
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
        auto gb_layout = new QFormLayout(gb_topOptions);
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

    connect(m_d->combo_condition, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int /*index*/) { m_d->on_trigger_condition_changed(); });

    connect(m_d->spin_irqLevel, qOverload<int>(&QSpinBox::valueChanged), this,
            [this]() { m_d->on_irq_level_changed(); });

    connect(m_d->buttonBox, &QDialogButtonBox::accepted, this, &EventConfigDialog::accept);
    connect(m_d->buttonBox, &QDialogButtonBox::rejected, this, &EventConfigDialog::reject);

    // irq widget
    auto irqWidget = new QWidget(this);
    auto irqLayout = new QFormLayout(irqWidget);
    irqLayout->addRow(QSL("IRQ Level"), m_d->spin_irqLevel);
    irqLayout->addRow(QSL("IRQ Vector"), m_d->spin_irqVector);

    struct TriggerConditionInfo
    {
        TriggerConditionInfo(const TriggerCondition &cond)
            : condition(cond)
            , displayName(TriggerConditionNames.value(cond))
        {
        }

        TriggerConditionInfo(const TriggerCondition &cond, const QString &name)
            : condition(cond)
            , displayName(name)
        {
        }

        TriggerCondition condition;
        QString displayName;
    };

    QVector<TriggerConditionInfo> conditions;

    switch (m_vmeControllerType)
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

        conditions = {TriggerCondition::Interrupt, TriggerCondition::NIM1,
                      TriggerCondition::Periodic};

        m_d->stack_options->addWidget(irqWidget);
        m_d->stack_options->addWidget(new QWidget); // nim has no special gui
        m_d->stack_options->addWidget(vmusbTimerWidget);
    }
    break;

    case VMEControllerType::SIS3153:
    {
        conditions = {TriggerCondition::Interrupt,        TriggerCondition::Periodic,
                      TriggerCondition::Input1RisingEdge, TriggerCondition::Input1FallingEdge,
                      TriggerCondition::Input2RisingEdge, TriggerCondition::Input2FallingEdge};

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

        for (int i = (int)TriggerCondition::Input1RisingEdge;
             i <= (int)TriggerCondition::Input2FallingEdge; ++i)
        {
            m_d->stack_options->addWidget(new QWidget); // no special gui for external triggers
        }
    }
    break;

    case VMEControllerType::MVLC_USB:
    case VMEControllerType::MVLC_ETH:
    {
        // IRQs
        {
            // Hide the IRQ Vector line. The vector is not used by the MVLC.
            irqLayout->labelForField(m_d->spin_irqVector)->hide();
            m_d->spin_irqVector->hide();

            irqLayout->addRow(QSL("Use Interrupt Acknowledge (IRQUseIACK)"), m_d->cb_irqUseIACK);
            m_d->cb_irqUseIACK->show();

            auto label =
                new QLabel(QSL("Note: enabling the IRQUseIACK option will make IRQ handling"
                               " slower but some VME modules might require it to work properly."));

            label->setWordWrap(true);
            irqLayout->addRow(label);
            m_d->stack_options->addWidget(irqWidget);
        }

        {
            m_d->spin_stackTimerPeriod = new QSpinBox;
            m_d->spin_stackTimerPeriod->setPrefix(QSL("Every "));
            m_d->spin_stackTimerPeriod->setSuffix(QSL(" ms"));
            m_d->spin_stackTimerPeriod->setMinimum(0);
            m_d->spin_stackTimerPeriod->setMaximum(0xffff);
            m_d->spin_stackTimerPeriod->setSingleStep(1.0);
            m_d->spin_stackTimerPeriod->setValue(1000);

            auto timerWidget = new QWidget;
            auto timerLayout = new QFormLayout(timerWidget);
            timerLayout->addRow(QSL("Period"), m_d->spin_stackTimerPeriod);
            auto label =
                new QLabel(QSL("MVLC StackTimers require MVLC firmware <b>FW0037</b> or later!"));
            label->setWordWrap(true);
            timerLayout->addRow(label);
            m_d->stack_options->addWidget(timerWidget);
        }

        // Trigger IO Condition
        {
            auto label = new QLabel(
                QSL("The event should be triggered via the MVLC Trigger I/O module.<br/><br/>"
                    "Use the Trigger I/O Editor to setup one of the <b>StackStart</b> "
                    "units to trigger execution of this events readout stack. Then connect "
                    "the StackStart unit to the desired activation signals."));
            label->setWordWrap(true);
            m_d->stack_options->addWidget(label);
        }

        // On Master Trigger (FW0037)
        {
            m_d->spin_mvlcSlaveTriggerIndex = new QSpinBox;
            m_d->spin_mvlcSlaveTriggerIndex->setMaximum(mvlc::stacks::SlaveTriggersCount - 1);

            auto optionsWidget = new QWidget;
            auto layout = new QFormLayout(optionsWidget);
            layout->addRow(QSL("Master Trigger Index"), m_d->spin_mvlcSlaveTriggerIndex);
            auto label = new QLabel(
                QSL("MVLC On Master Trigger requires MVLC firmware <b>FW0037</b> or later!<br/>"
                    " Works on the master itself and on connected secondary crates."));
            label->setWordWrap(true);
            layout->addRow(label);
            m_d->stack_options->addWidget(optionsWidget);
        }

        conditions = {
            TriggerCondition::Interrupt,
            {TriggerCondition::MvlcStackTimer, QSL("Periodic")},
            TriggerCondition::TriggerIO,
            TriggerCondition::MvlcOnSlaveTrigger,
        };
    }
    break;
    }

    for (auto condInfo: conditions)
    {
        m_d->combo_condition->addItem(condInfo.displayName, static_cast<s32>(condInfo.condition));
    }

    loadFromConfig();
}

EventConfigDialog::~EventConfigDialog() { delete m_d; }

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

    switch (m_vmeControllerType)
    {
    case VMEControllerType::VMUSB:
    {
        m_d->spin_timerPeriod->setValue(config->scalerReadoutPeriod * 0.5);
        m_d->spin_vmusbTimerFrequency->setValue(config->scalerReadoutFrequency);
    }
    break;
    case VMEControllerType::SIS3153:
    {
        m_d->spin_timerPeriod->setValue(
            config->triggerOptions.value(QSL("sis3153.timer_period"), 0.0).toDouble());
    }
    break;

    case VMEControllerType::MVLC_USB:
    case VMEControllerType::MVLC_ETH:
    {
        m_d->spin_stackTimerPeriod->setValue(
            config->triggerOptions.value(QSL("mvlc.stacktimer_period"), 1000u).toUInt());

        m_d->spin_mvlcSlaveTriggerIndex->setValue(
            config->triggerOptions.value(QSL("mvlc.slavetrigger_index"), 0u).toULongLong());
    }
    break;
    }
}

void EventConfigDialog::saveToConfig()
{
    auto config = m_config;

    config->setObjectName(m_d->le_name->text());
    config->triggerCondition =
        static_cast<TriggerCondition>(m_d->combo_condition->currentData().toInt());
    config->irqLevel = static_cast<uint8_t>(m_d->spin_irqLevel->value());
    config->irqVector = static_cast<uint8_t>(m_d->spin_irqVector->value());
    config->setVariables(m_d->variableEditor->getVariables());

    switch (m_vmeControllerType)
    {
    case VMEControllerType::VMUSB:
    {
        config->scalerReadoutPeriod = static_cast<uint8_t>(m_d->spin_timerPeriod->value() * 2.0);
        config->scalerReadoutFrequency =
            static_cast<uint16_t>(m_d->spin_vmusbTimerFrequency->value());
    }
    break;

    case VMEControllerType::SIS3153:
    {
        config->triggerOptions[QSL("sis3153.timer_period")] = m_d->spin_timerPeriod->value();
    }
    break;

    case VMEControllerType::MVLC_USB:
    case VMEControllerType::MVLC_ETH:
        config->triggerOptions["IRQUseIACK"] = m_d->cb_irqUseIACK->isChecked();
        if (config->triggerCondition == TriggerCondition::MvlcStackTimer)
        {
            config->triggerOptions["mvlc.stacktimer_period"] = m_d->spin_stackTimerPeriod->value();
        }
        else if (config->triggerCondition == TriggerCondition::MvlcOnSlaveTrigger)
        {
            config->triggerOptions["mvlc.slavetrigger_index"] =
                m_d->spin_mvlcSlaveTriggerIndex->value();
        }
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
    QComboBox *typeCombo_;
    QLineEdit *nameEdit_;
    QLineEdit *addressEdit_;
    ModuleConfig *module_;
    const VMEConfig *vmeConfig_;
    QVector<vats::VMEModuleMeta> moduleMetas_;
    VariableEditorWidget *variableEditor_;
    ModuleEventHeaderFiltersEditor *eventHeaderFiltersEditor_;
};

//
// ModuleConfigDialog
//
ModuleConfigDialog::ModuleConfigDialog(ModuleConfig *mod,
                                       [[maybe_unused]] const EventConfig *parentEvent,
                                       const VMEConfig *vmeConfig, QWidget *parent)
    : QDialog(parent)
    , d(std::make_unique<Private>())
{
    assert(mod);
    assert(parentEvent);
    assert(vmeConfig);

    resize(800, 600);

    d->module_ = mod;
    d->vmeConfig_ = vmeConfig;
    d->variableEditor_ = new VariableEditorWidget;
    d->eventHeaderFiltersEditor_ = new ModuleEventHeaderFiltersEditor;

    setWindowTitle(QSL("Module Config"));
    MVMETemplates templates = read_templates();
    d->moduleMetas_ = templates.moduleMetas;

    // Add the meta stored with the module itself to the list of metas. Might be
    // a duplicate entry in case its a builtin-module but the code does not
    // care.
    if (auto thisMeta = mod->getModuleMeta(); !thisMeta.typeName.isEmpty())
        d->moduleMetas_.push_back(thisMeta);

    /* Sort by vendorName and then displayName, giving the vendorName "mesytec"
     * the highest priority. Modules without a vendor name always have to the
     * lowest prio. */
    std::sort(d->moduleMetas_.begin(), d->moduleMetas_.end(),
              [](const VMEModuleMeta &a, const VMEModuleMeta &b)
              {
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

    d->typeCombo_ = new QComboBox;
    int typeComboIndex = -1;
    QString currentVendor;

    for (const auto &mm: d->moduleMetas_)
    {
        if (currentVendor.isNull())
            currentVendor = mm.vendorName;

        if (mm.vendorName != currentVendor)
        {
            d->typeCombo_->insertSeparator(d->typeCombo_->count());
            currentVendor = mm.vendorName;
        }

        d->typeCombo_->addItem(mm.displayName, mm.typeName);

        if (mm.typeName == d->module_->getModuleMeta().typeName)
            typeComboIndex = d->typeCombo_->count() - 1;
    }

    if (typeComboIndex < 0)
    {
        if (!d->module_->getModuleMeta().displayName.isEmpty())
        {
            d->typeCombo_->insertSeparator(d->typeCombo_->count());
            d->typeCombo_->addItem(d->module_->getModuleMeta().displayName);
            typeComboIndex = d->typeCombo_->count() - 1;
        }
        else
            typeComboIndex = 0;
    }

    d->nameEdit_ = new QLineEdit;

    d->addressEdit_ = make_vme_address_edit(d->module_->getBaseAddress());

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QFormLayout(this);

    const bool isNewModule = !d->module_->parent();

    if (!isNewModule)
    {
        setAllowTypeChange(false);
    }

    auto tabs = new QTabWidget;
    tabs->addTab(d->variableEditor_, "VME Script Variables");
    tabs->addTab(d->eventHeaderFiltersEditor_, "Event Header Filters");

    layout->addRow("Type", d->typeCombo_);
    layout->addRow("Name", d->nameEdit_);
    layout->addRow("Address", d->addressEdit_);
    layout->addRow(tabs);
    layout->addRow(bb);

    auto onTypeComboIndexChanged = [this, isNewModule](int /*index*/)
    {
        auto typeName = d->typeCombo_->currentData().toString();

        auto it = std::find_if(d->moduleMetas_.begin(), d->moduleMetas_.end(),
                               [typeName](const auto &mm) { return mm.typeName == typeName; });

        if (it == d->moduleMetas_.end())
        {
            d->nameEdit_->setText(d->module_->objectName());
            d->addressEdit_->setText(
                QString("0x%1").arg(d->module_->getBaseAddress(), 8, 16, QChar('0')));
            return;
        }

        const auto &mm(*it);
        QString name = d->module_->objectName();

        if (name.isEmpty())
        {
            name = make_unique_module_name(mm.typeName, d->vmeConfig_);
        }

        d->nameEdit_->setText(name);

        if (isNewModule)
        {
            d->addressEdit_->setText(QString("0x%1").arg(mm.vmeAddress, 8, 16, QChar('0')));
            d->variableEditor_->setVariables(
                mvme::vme_config::variable_symboltable_from_module_meta(mm));
            d->eventHeaderFiltersEditor_->setData(mm.eventHeaderFilters);
        }
        else
        {
            d->eventHeaderFiltersEditor_->setData(d->module_->getModuleMeta().eventHeaderFilters);
        }
    };

    connect(d->typeCombo_, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            onTypeComboIndexChanged);

    if (typeComboIndex >= 0)
    {
        d->typeCombo_->setCurrentIndex(typeComboIndex);
        onTypeComboIndexChanged(typeComboIndex);
    }

    auto updateOkButton = [=]()
    {
        bool isOk = (d->addressEdit_->hasAcceptableInput() && d->typeCombo_->count() > 0);
        bb->button(QDialogButtonBox::Ok)->setEnabled(isOk);
    };

    connect(d->addressEdit_, &QLineEdit::textChanged, updateOkButton);
    updateOkButton();
}

ModuleConfigDialog::~ModuleConfigDialog() {}

ModuleConfig *ModuleConfigDialog::getModule() const
{
    return d->module_;
}

void ModuleConfigDialog::setAllowTypeChange(bool allow)
{
    d->typeCombo_->setEnabled(allow);
}

void ModuleConfigDialog::accept()
{
    auto typeName = d->typeCombo_->currentData().toString();
    auto it = std::find_if(d->moduleMetas_.begin(), d->moduleMetas_.end(),
                            [typeName](const auto &mm) { return mm.typeName == typeName; });

    // Note: the meta info of existing modules is _not_ updated in here. Instead
    // when loading a VMEConfig from file the meta info is updated from the
    // template system (ModuleConfig::read_impl()).

    if (it != d->moduleMetas_.end())
    {
        const auto &mm(*it);
        d->module_->setModuleMeta(mm);

        if (!mm.templateFile.isEmpty())
        {
            // New style template from a single json file.
            mvme::vme_config::load_moduleconfig_from_modulejson(*d->module_, mm.moduleJson);
        }
        else if (!mm.templatePath.isEmpty())
        {
            // Old style template from multiple .vme files
            d->module_->getReadoutScript()->setObjectName(mm.templates.readout.name);
            d->module_->getReadoutScript()->setScriptContents(mm.templates.readout.contents);

            d->module_->getResetScript()->setObjectName(mm.templates.reset.name);
            d->module_->getResetScript()->setScriptContents(mm.templates.reset.contents);

            for (const auto &vmeTemplate: mm.templates.init)
            {
                d->module_->addInitScript(
                    new VMEScriptConfig(vmeTemplate.name, vmeTemplate.contents));
            }
        }
    }

    // Make sure there's at least one init script present.
    if (d->module_->getInitScripts().isEmpty())
    {
        d->module_->addInitScript(new VMEScriptConfig("Module Init", QString()));
    }

    d->module_->setObjectName(d->nameEdit_->text());
    d->module_->setBaseAddress(d->addressEdit_->text().toUInt(nullptr, 16));
    d->module_->setVariables(d->variableEditor_->getVariables());

    QDialog::accept();
}

QLineEdit *make_vme_address_edit(u32 address, QWidget *parent)
{
    auto addressEdit = new QLineEdit(parent);

    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    addressEdit->setFont(font);
    addressEdit->setInputMask("\\0\\xHHHH\\0\\0\\0\\0");
    addressEdit->setText(QString("0x%1").arg(address, 8, 16, QChar('0')));

    return addressEdit;
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

QString info_text(const VMEConfig *config)
{
    if (!is_mvlc_controller(config->getControllerType()))
        return {};

    auto settings = config->getControllerSettings();
    QString ret;

    if (config->getControllerType() == VMEControllerType::MVLC_ETH)
    {
        ret = QSL("eth://%1").arg(settings.value("mvlc_hostname").toString());
    }
    else if (settings.value("method").toString() == "by_index")
    {
        ret = QSL("usb://@%1").arg(settings.value("index").toString());
    }
    else if (settings.value("method").toString() == "by_serial")
    {
        ret = QSL("usb://%1").arg(settings.value("serial").toString());
    }
    else if (settings.value("method").toString() == "first")
    {
        ret = QSL("usb://");
    }

    ret = QSL("MVLC ") + ret;

    return ret;
}

QString info_text(const EventConfig *config)
{
    QString infoText;

    switch (config->triggerCondition)
    {
    case TriggerCondition::Interrupt:
    {
        infoText = QString("Trigger=IRQ%1").arg(config->irqLevel);
    }
    break;
    case TriggerCondition::NIM1:
    {
        infoText = QSL("Trigger=NIM");
    }
    break;
    case TriggerCondition::Periodic:
    {
        infoText = QSL("Trigger=Periodic");
        // Note 251028: Periodic triggers via trigger io removed from the UI.
        // This used to display the periods value and unit.
        if (auto vmeConfig = config->getVMEConfig();
            vmeConfig && is_mvlc_controller(vmeConfig->getControllerType()))
        {
            infoText = TriggerConditionNames.value(TriggerCondition::TriggerIO);
        }
    }
    break;
    case TriggerCondition::MvlcStackTimer:
    {
        infoText = QSL("Trigger=%1, every %2 ms")
                       .arg(TriggerConditionNames.value(config->triggerCondition))
                       .arg(config->triggerOptions["mvlc.stacktimer_period"].toULongLong());
        ;
    }
    break;
    case TriggerCondition::MvlcOnSlaveTrigger:
    {
        infoText = QSL("Trigger=%1, TriggerIndex=%2")
                       .arg(TriggerConditionNames.value(config->triggerCondition))
                       .arg(config->triggerOptions["mvlc.slavetrigger_index"].toULongLong());
        break;
    }
    default:
    {
        infoText = QString("Trigger=%1").arg(TriggerConditionNames.value(config->triggerCondition));
    }
    break;
    }

    auto vars = config->getVariables();

    if (auto mcst = vars.value("mesy_mcst").value; !mcst.isEmpty())
        infoText += QSL(", mcst=%1").arg(mcst);

    return infoText;
}

QString info_text(const ModuleConfig *config)
{
    QString infoText = QString("Type=%1, Address=0x%2")
                           .arg(config->getModuleMeta().displayName)
                           .arg(config->getBaseAddress(), 8, 16, QChar('0'));

    return infoText;
}

QWidget *DataFilterEditItemDelegate::createEditor(QWidget *parent,
                                                  const QStyleOptionViewItem &option,
                                                  const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return new DataFilterEdit(parent);
}

static const QByteArray DefaultHeaderFilter = "0100 XXX0 MMMM MMMM XXXX XXSS SSSS SSSS";
static const QByteArray DefaultHeaderFilterDesc = "Example: mesytec VME module header (non-sampling mode)";

ModuleEventHeaderFiltersTable::ModuleEventHeaderFiltersTable(QWidget *parent)
    : QTableWidget(parent)
{
    setColumnCount(2);
    setHorizontalHeaderLabels({QSL("Filter String"), QSL("Description")});
    horizontalHeader()->setStretchLastSection(true);
    verticalHeader()->setVisible(false);
    setItemDelegateForColumn(0, new DataFilterEditItemDelegate(this));
    setContextMenuPolicy(Qt::CustomContextMenu);

    auto add_entry = [this]
    {
        appendRow({DefaultHeaderFilter, DefaultHeaderFilterDesc});
    };

    auto remove_entry = [this]
    {
        if (auto idx = selectionModel()->currentIndex(); idx.isValid())
        {
            removeRow(idx.row());
        }
    };

    auto context_menu_handler = [this, add_entry, remove_entry] (const QPoint &pos)
    {
        QMenu menu;
        auto item = itemAt(pos);

        if (item)
            menu.addAction(make_copy_to_clipboard_action(this, &menu));
        menu.addAction(QIcon(":/list_add.png"), QSL("Add new entry"), this, add_entry);
        if (item)
            menu.addAction(QIcon(":/list_remove.png"), QSL("Delete selected entry"), this, remove_entry);

        if (!menu.isEmpty())
            menu.exec(this->mapToGlobal(pos));
    };

    connect(this, &QTableWidget::customContextMenuRequested, this, context_menu_handler);

    auto on_item_changed = [this](QTableWidgetItem *)
    {
        resizeColumnsToContents();
        resizeRowsToContents();
    };

    connect(this, &QTableWidget::itemChanged, this, on_item_changed);
}

void ModuleEventHeaderFiltersTable::appendRow(const vats::VMEModuleEventHeaderFilter &filterDef)
{
    auto row = rowCount();
    setRowCount(row + 1);
    auto theFont = make_monospace_font();
    theFont.setPointSize(9);

    auto item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, generate_pretty_filter_string(filterDef.filterString));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFont(theFont);
    setItem(row, 0, item);

    auto item2 = new QTableWidgetItem;
    item2->setData(Qt::DisplayRole, filterDef.description);
    setItem(row, 1, item2);

    resizeColumnToContents(0);
    resizeRowsToContents();
}

void ModuleEventHeaderFiltersTable::setData(const std::vector<vats::VMEModuleEventHeaderFilter> &filterDefs)
{
    clearContents();
    setRowCount(0);

    for (const auto &filterDef: filterDefs)
        appendRow(filterDef);

    resizeColumnsToContents();
    resizeRowsToContents();
}

std::vector<vats::VMEModuleEventHeaderFilter> ModuleEventHeaderFiltersTable::getData() const
{
    std::vector<vats::VMEModuleEventHeaderFilter> result;
    result.reserve(rowCount());

    for (int i = 0; i < rowCount(); ++i)
    {
        auto item = this->item(i, 0);
        auto item2 = this->item(i, 1);

        if (item && item2)
        {
            vats::VMEModuleEventHeaderFilter filterDef;
            auto filterString = item->text();
            filterString.remove(QChar(' '));

            if (filterString.isEmpty())
                continue;

            filterDef.filterString = filterString.toLocal8Bit();
            filterDef.description = item2->text();
            result.push_back(filterDef);
        }
    }

    return result;
}

ModuleEventHeaderFiltersEditor::ModuleEventHeaderFiltersEditor(QWidget *parent)
    : QWidget(parent)
    , m_table_(new ModuleEventHeaderFiltersTable(this))
    , pb_addEntry_(new QPushButton(QIcon::fromTheme(":/list_add.png"), QSL("&Add new entry")))
    , pb_removeEntry_(new QPushButton(QIcon::fromTheme(":/list_remove.png"), QSL("&Delete selected entry")))
{
    auto label = new QLabel(QSL(
            "Bit-level filters used to split incoming multi-event module data into individual events.<br/>"
            "The marker 'S' is used to extract the number of data words contained in the event. "
            "If multiple filters are defined, they are tried in order until one matches. "
            "Only has an effect if 'Multi Event Processing' is enabled in the analysis. "
            "Changes become active on the next DAQ/Replay start."
            ));
    label->setWordWrap(true);


    pb_removeEntry_->setEnabled(false);
    connect(pb_addEntry_, &QPushButton::clicked, this, &ModuleEventHeaderFiltersEditor::addEntry);

    auto remove_selected_entry = [this]
    {
        if (auto idx = m_table_->selectionModel()->currentIndex(); idx.isValid())
        {
            m_table_->removeRow(idx.row());
        }
    };

    connect(pb_removeEntry_, &QPushButton::clicked, this, remove_selected_entry);

    connect(m_table_, &QTableWidget::itemSelectionChanged, this, [this]
    {
        auto sm = m_table_->selectionModel();
        auto idx = sm->currentIndex();
        pb_removeEntry_->setEnabled(idx.isValid());
    });

    auto actionButtonsLayout = make_hbox();
    actionButtonsLayout->addWidget(pb_addEntry_);
    actionButtonsLayout->addWidget(pb_removeEntry_);
    actionButtonsLayout->addStretch(1);


    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(label);
    layout->addWidget(m_table_);
    layout->addLayout(actionButtonsLayout);
    setLayout(layout);
}


void ModuleEventHeaderFiltersEditor::setData(const std::vector<vats::VMEModuleEventHeaderFilter> &filterDefs)
{
    m_table_->setData(filterDefs);
}

std::vector<vats::VMEModuleEventHeaderFilter> ModuleEventHeaderFiltersEditor::getData() const
{
    return m_table_->getData();
}

void ModuleEventHeaderFiltersEditor::addEntry()
{
    m_table_->appendRow({DefaultHeaderFilter, DefaultHeaderFilterDesc});
}
