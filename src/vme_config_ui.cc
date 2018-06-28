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
#include "vme_config_ui.h"
#include "vme_config.h"
#include "mvme_context.h"
#include "vme_script.h"
#include "analysis/analysis.h"
#include "qt-collapsible-section/Section.h"
#include "data_filter_edit.h"

#include <cmath>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
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
#include <QStandardPaths>
#include <QThread>

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

    QDoubleSpinBox *spin_vmusbTimerPeriod,
                   *spin_sis3153TimerPeriod;
};

EventConfigDialog::EventConfigDialog(MVMEContext *context, VMEController *controller,
                                     EventConfig *config, QWidget *parent)
    : QDialog(parent)
    , m_d(new EventConfigDialogPrivate)
    , m_context(context)
    , m_controller(controller)
    , m_config(config)
{
    m_d->le_name = new QLineEdit;
    m_d->combo_condition = new QComboBox;
    m_d->stack_options = new QStackedWidget;
    m_d->buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    m_d->spin_irqLevel = new QSpinBox(this);
    m_d->spin_irqLevel->setMinimum(1);
    m_d->spin_irqLevel->setMaximum(7);

    m_d->spin_irqVector = new QSpinBox(this);
    m_d->spin_irqVector->setMaximum(255);

    auto gb_nameAndCond = new QGroupBox;
    auto gb_layout   = new QFormLayout(gb_nameAndCond);
    gb_layout->setContentsMargins(2, 2, 2, 2);
    gb_layout->addRow(QSL("Name"), m_d->le_name);
    gb_layout->addRow(QSL("Condition"), m_d->combo_condition);


    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(gb_nameAndCond);
    layout->addWidget(m_d->stack_options);
    layout->addWidget(m_d->buttonBox);

    connect(m_d->combo_condition,
            static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            m_d->stack_options, &QStackedWidget::setCurrentIndex);

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
                m_d->spin_vmusbTimerPeriod = new QDoubleSpinBox;
                m_d->spin_vmusbTimerPeriod->setPrefix(QSL("Every "));
                m_d->spin_vmusbTimerPeriod->setSuffix(QSL(" seconds"));
                m_d->spin_vmusbTimerPeriod->setMaximum(127.5);
                m_d->spin_vmusbTimerPeriod->setDecimals(1);
                m_d->spin_vmusbTimerPeriod->setSingleStep(0.5);

                m_d->spin_vmusbTimerFrequency = new QSpinBox;
                m_d->spin_vmusbTimerFrequency->setPrefix(QSL("Every "));
                m_d->spin_vmusbTimerFrequency->setSuffix(QSL(" events"));
                m_d->spin_vmusbTimerFrequency->setMaximum(65535);

                // vmusb timer widget
                auto vmusbTimerWidget = new QWidget;
                auto vmusbTimerLayout = new QFormLayout(vmusbTimerWidget);
                vmusbTimerLayout->addRow(QSL("Period"), m_d->spin_vmusbTimerPeriod);
                vmusbTimerLayout->addRow(QSL("Frequency"), m_d->spin_vmusbTimerFrequency);

                conditions = { TriggerCondition:: Interrupt,
                    TriggerCondition::NIM1, TriggerCondition::Periodic };

                m_d->stack_options->addWidget(irqWidget);
                m_d->stack_options->addWidget(new QWidget);         // nim has no special gui
                m_d->stack_options->addWidget(vmusbTimerWidget);
            } break;

        case VMEControllerType::SIS3153:
            {
                conditions = { TriggerCondition:: Interrupt, TriggerCondition::Periodic,
                    TriggerCondition::Input1RisingEdge, TriggerCondition::Input1FallingEdge,
                    TriggerCondition::Input2RisingEdge, TriggerCondition::Input2FallingEdge
                };

                m_d->spin_sis3153TimerPeriod = new QDoubleSpinBox;
                m_d->spin_sis3153TimerPeriod->setPrefix(QSL("Every "));
                m_d->spin_sis3153TimerPeriod->setSuffix(QSL(" seconds"));
                m_d->spin_sis3153TimerPeriod->setMinimum(0.1);
                m_d->spin_sis3153TimerPeriod->setMaximum(6.5);
                m_d->spin_sis3153TimerPeriod->setDecimals(1);
                m_d->spin_sis3153TimerPeriod->setSingleStep(0.1);
                m_d->spin_sis3153TimerPeriod->setValue(1.0);

                auto timerWidget = new QWidget;
                auto timerLayout = new QFormLayout(timerWidget);
                timerLayout->addRow(QSL("Period"), m_d->spin_sis3153TimerPeriod);

                m_d->stack_options->addWidget(irqWidget);
                m_d->stack_options->addWidget(timerWidget);
                for (int i=(int)TriggerCondition::Input1RisingEdge;
                     i<=(int)TriggerCondition::Input2FallingEdge;
                     ++i)
                {
                    m_d->stack_options->addWidget(new QWidget); // no special gui for external triggers
                }
            } break;
    }

    for (auto cond: conditions)
    {
        m_d->combo_condition->addItem(TriggerConditionNames.value(cond, QSL("Unknown")),
                                      static_cast<s32>(cond));
    }

    loadFromConfig();

    auto handleContextStateChange = [this] {
        auto daqState = m_context->getDAQState();
        auto globalMode = m_context->getMode();
        setReadOnly(daqState != DAQState::Idle || globalMode == GlobalMode::ListFile);
    };

    connect(context, &MVMEContext::daqStateChanged, this, handleContextStateChange);
    connect(context, &MVMEContext::modeChanged, this, handleContextStateChange);

    handleContextStateChange();
}

EventConfigDialog::~EventConfigDialog()
{
    delete m_d;
}

void EventConfigDialog::loadFromConfig()
{
    auto config = m_config;

    m_d->le_name->setText(config->objectName());

    int condIndex = m_d->combo_condition->findData(static_cast<s32>(config->triggerCondition));
    if (condIndex >= 0)
    {
        m_d->combo_condition->setCurrentIndex(condIndex);
    }

    m_d->spin_irqLevel->setValue(config->irqLevel);
    m_d->spin_irqVector->setValue(config->irqVector);

    switch (m_context->getVMEController()->getType())
    {
        case VMEControllerType::VMUSB:
            {
                m_d->spin_vmusbTimerPeriod->setValue(config->scalerReadoutPeriod * 0.5);
                m_d->spin_vmusbTimerFrequency->setValue(config->scalerReadoutFrequency);
            } break;
        case VMEControllerType::SIS3153:
            {
                m_d->spin_sis3153TimerPeriod->setValue(
                    config->triggerOptions.value(QSL("sis3153.timer_period"), 0.0).toDouble());

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

    switch (m_context->getVMEController()->getType())
    {
        case VMEControllerType::VMUSB:
            {
                config->scalerReadoutPeriod = static_cast<uint8_t>(m_d->spin_vmusbTimerPeriod->value() * 2.0);
                config->scalerReadoutFrequency = static_cast<uint16_t>(m_d->spin_vmusbTimerFrequency->value());
            } break;

        case VMEControllerType::SIS3153:
            {
                config->triggerOptions[QSL("sis3153.timer_period")] = m_d->spin_sis3153TimerPeriod->value();
            } break;
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

//
// ModuleConfigDialog
//
ModuleConfigDialog::ModuleConfigDialog(MVMEContext *context, ModuleConfig *module, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
    , m_module(module)
{
    setWindowTitle(QSL("Module Config"));
    MVMETemplates templates = read_templates();
    m_moduleMetas = templates.moduleMetas;

    /* Sort by vendorName and then displayName, giving the vendorName "mesytec"
     * the highest priority. */
    qSort(m_moduleMetas.begin(), m_moduleMetas.end(), [](const VMEModuleMeta &a, const VMEModuleMeta &b) {
        if (a.vendorName == b.vendorName)
            return a.displayName < b.displayName;

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

        typeCombo->addItem(mm.displayName);

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

    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QFormLayout(this);
    layout->addRow("Type", typeCombo);
    layout->addRow("Name", nameEdit);
    layout->addRow("Address", addressEdit);
    layout->addRow(bb);

    auto onTypeComboIndexChanged = [this](int index)
    {
        Q_ASSERT(0 <= index && index < m_moduleMetas.size());

        const auto &mm(m_moduleMetas[index]);
        QString name = m_module->objectName();

        if (name.isEmpty())
        {
            name = m_context->getUniqueModuleName(mm.typeName);
        }

        nameEdit->setText(name);
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

void ModuleConfigDialog::accept()
{
    bool ok;
    Q_ASSERT(typeCombo->currentIndex() >= 0 && typeCombo->currentIndex() < m_moduleMetas.size());
    const auto &mm(m_moduleMetas[typeCombo->currentIndex()]);
    m_module->setModuleMeta(mm);
    m_module->setObjectName(nameEdit->text());
    m_module->setBaseAddress(addressEdit->text().toUInt(&ok, 16));

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

namespace
{
    bool saveAnalysisConfigImpl(analysis::Analysis *analysis_ng, const QString &fileName)
    {
        QJsonObject json;
        {
            QJsonObject destObject;
            analysis_ng->write(destObject);
            json[QSL("AnalysisNG")] = destObject;
        }
        return gui_write_json_file(fileName, QJsonDocument(json));
    }
}

QPair<bool, QString> gui_saveAnalysisConfig(analysis::Analysis *analysis_ng,
                                            const QString &fileName, QString startPath,
                                            QString fileFilter)
{
    if (fileName.isEmpty())
        return gui_saveAnalysisConfigAs(analysis_ng, startPath, fileFilter);

    if (saveAnalysisConfigImpl(analysis_ng, fileName))
    {
        return qMakePair(true, fileName);
    }
    return qMakePair(false, QString());
}

QPair<bool, QString> gui_saveAnalysisConfigAs(analysis::Analysis *analysis_ng,
                                              QString path, QString fileFilter)
{
    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getSaveFileName(nullptr, QSL("Save analysis config"),
                                                    path, fileFilter);

    if (fileName.isEmpty())
        return qMakePair(false, QString());

    QFileInfo fi(fileName);

    if (fi.completeSuffix().isEmpty())
        fileName += QSL(".analysis");

    if (saveAnalysisConfigImpl(analysis_ng, fileName))
    {
        return qMakePair(true, fileName);
    }

    return qMakePair(false, QString());
}
