#include "config_ui.h"
#include "ui_event_config_dialog.h"
#include "mvme_config.h"
#include "mvme_context.h"
#include "vme_script.h"
#include "analysis/analysis.h"

#include <cmath>

#include <QMenu>
#include <QStandardPaths>
#include <QSettings>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QStandardItemModel>
#include <QCloseEvent>
#include <QScrollBar>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonDocument>

//
// EventConfigDialog
//
EventConfigDialog::EventConfigDialog(MVMEContext *context, EventConfig *config, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EventConfigDialog)
    , m_context(context)
    , m_config(config)
{
    ui->setupUi(this);
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
    delete ui;
}

void EventConfigDialog::loadFromConfig()
{
    auto config = m_config;

    ui->le_name->setText(config->objectName());
    ui->combo_triggerCondition->setCurrentIndex(
        static_cast<int>(config->triggerCondition));

    ui->spin_period->setValue(config->scalerReadoutPeriod * 0.5);
    ui->spin_frequency->setValue(config->scalerReadoutFrequency);
    ui->spin_irqLevel->setValue(config->irqLevel);
    ui->spin_irqVector->setValue(config->irqVector);
}

void EventConfigDialog::saveToConfig()
{
    auto config = m_config;

    config->setObjectName(ui->le_name->text());
    config->triggerCondition = static_cast<TriggerCondition>(ui->combo_triggerCondition->currentIndex());
    config->scalerReadoutPeriod = static_cast<uint8_t>(ui->spin_period->value() * 2.0);
    config->scalerReadoutFrequency = static_cast<uint16_t>(ui->spin_frequency->value());
    config->irqLevel = static_cast<uint8_t>(ui->spin_irqLevel->value());
    config->irqVector = static_cast<uint8_t>(ui->spin_irqVector->value());
    config->setModified(true);
}

void EventConfigDialog::accept()
{
    saveToConfig();
    QDialog::accept();
}

void EventConfigDialog::setReadOnly(bool readOnly)
{
    ui->le_name->setEnabled(!readOnly);
    ui->combo_triggerCondition->setEnabled(!readOnly);
    ui->stackedWidget->setEnabled(!readOnly);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!readOnly);
}

//
// ModuleConfigDialog
//
ModuleConfigDialog::ModuleConfigDialog(MVMEContext *context, ModuleConfig *module, QWidget *parent)
    : QDialog(parent)
    , m_context(context)
    , m_module(module)
{
    typeCombo = new QComboBox;

    int typeComboIndex = 0;

    for (auto type: VMEModuleTypeNames.keys())
    {
        typeCombo->addItem(VMEModuleTypeNames[type], QVariant::fromValue(static_cast<int>(type)));
        if (type == module->type)
            typeComboIndex = typeCombo->count() - 1;
    }

    typeCombo->setCurrentIndex(typeComboIndex);

    nameEdit = new QLineEdit;

    auto onTypeComboIndexChanged = [=](int index)
    {
        auto currentType = static_cast<VMEModuleType>(typeCombo->currentData().toInt());
        QString name = context->getUniqueModuleName(VMEModuleShortNames[currentType]);
        nameEdit->setText(name);
    };

    onTypeComboIndexChanged(typeComboIndex);

    if (!module->objectName().isEmpty())
    {
        nameEdit->setText(module->objectName());
    }

    connect(typeCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            this, onTypeComboIndexChanged);


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

    connect(addressEdit, &QLineEdit::textChanged, [=](const QString &) {
        bb->button(QDialogButtonBox::Ok)->setEnabled(addressEdit->hasAcceptableInput());
    });
}

void ModuleConfigDialog::accept()
{
    bool ok;
    m_module->type = static_cast<VMEModuleType>(typeCombo->currentData().toInt());
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
    bool saveAnalysisConfigImpl(AnalysisConfig *config, analysis::Analysis *analysis_ng, const QString &fileName)
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

QPair<bool, QString> saveAnalysisConfig(AnalysisConfig *config, analysis::Analysis *analysis_ng, const QString &fileName, QString startPath, QString fileFilter)
{
    if (fileName.isEmpty())
        return saveAnalysisConfigAs(config, analysis_ng, startPath, fileFilter);

    if (saveAnalysisConfigImpl(config, analysis_ng, fileName))
    {
        return qMakePair(true, fileName);
    }
    return qMakePair(false, QString());
}

QPair<bool, QString> saveAnalysisConfigAs(AnalysisConfig *config, analysis::Analysis *analysis_ng, QString path, QString fileFilter)
{
    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString fileName = QFileDialog::getSaveFileName(nullptr, QSL("Save analysis config"), path, fileFilter);

    if (fileName.isEmpty())
        return qMakePair(false, QString());

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
        fileName += QSL(".analysis");

    if (saveAnalysisConfigImpl(config, analysis_ng, fileName))
    {
        return qMakePair(true, fileName);
    }

    return qMakePair(false, QString());
}
