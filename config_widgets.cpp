#include "config_widgets.h"
#include "ui_module_config_dialog.h"
#include "ui_event_config_dialog.h"
#include "mvme_config.h"
#include "mvme_context.h"
#include "vmusb.h"

#include <QMenu>
#include <QStandardPaths>
#include <QSettings>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QStandardItemModel>
#include <QCloseEvent>

EventConfigDialog::EventConfigDialog(MVMEContext *context, EventConfig *config, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EventConfigDialog)
    , m_context(context)
    , m_config(config)
{
    ui->setupUi(this);
    loadFromConfig();

    connect(context, &MVMEContext::eventConfigAboutToBeRemoved, this, [=](EventConfig *eventConfig) {
        if (eventConfig == config)
        {
            close();
        }
    });
}

void EventConfigDialog::loadFromConfig()
{
    auto config = m_config;

    ui->le_name->setText(config->getName());
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

    config->setName(ui->le_name->text());
    config->triggerCondition = static_cast<TriggerCondition>(ui->combo_triggerCondition->currentIndex());
    config->scalerReadoutPeriod = static_cast<uint8_t>(ui->spin_period->value() * 2);
    config->scalerReadoutFrequency = static_cast<uint16_t>(ui->spin_frequency->value());
    config->irqLevel = static_cast<uint8_t>(ui->spin_irqLevel->value());
    config->irqVector = static_cast<uint8_t>(ui->spin_irqVector->value());
    config->setModified();
}

void EventConfigDialog::accept()
{
    saveToConfig();
    QDialog::accept();
}

enum class ModuleListType
{
    Parameters,
    Readout,
    StartDAQ,
    StopDAQ,
    Reset,
    ReadoutStack,
    TypeCount
};

QString *getConfigString(ModuleListType type, ModuleConfig *config)
{
    switch (type)
    {
        case ModuleListType::Parameters:
            return &config->initParameters;
        case ModuleListType::Readout:
            return &config->initReadout;
        case ModuleListType::StartDAQ:
            return &config->initStartDaq;
        case ModuleListType::StopDAQ:
            return &config->initStopDaq;
        case ModuleListType::Reset:
            return &config->initReset;
        case ModuleListType::ReadoutStack:
            return &config->readoutStack;
        case ModuleListType::TypeCount:
            break;
    }

    return nullptr;
}

ModuleConfigDialog::ModuleConfigDialog(MVMEContext *context, ModuleConfig *config, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ModuleConfigDialog)
    , m_context(context)
    , m_config(config)
{
    ui->setupUi(this);

    connect(context, &MVMEContext::moduleAboutToBeRemoved, this, [=](ModuleConfig *module) {
        if (module == config)
        {
            close();
        }
    });

    auto model = qobject_cast<QStandardItemModel *>(ui->combo_listType->model());

    // Module initialization
    {
        ui->combo_listType->addItem("----- Initialization -----");
        auto item = model->item(ui->combo_listType->count() - 1);
        item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
    }

    ui->combo_listType->addItem("Module Reset", QVariant::fromValue(static_cast<int>(ModuleListType::Reset)));
    ui->combo_listType->addItem("Module Init", QVariant::fromValue(static_cast<int>(ModuleListType::Parameters)));
    ui->combo_listType->addItem("Readout Settings", QVariant::fromValue(static_cast<int>(ModuleListType::Readout)));

    // Readout loop
    {
        ui->combo_listType->addItem("----- Readout -----");
        auto item = model->item(ui->combo_listType->count() - 1);
        item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
    }
    ui->combo_listType->addItem("Start DAQ", QVariant::fromValue(static_cast<int>(ModuleListType::StartDAQ)));
    ui->combo_listType->addItem("Readout Stack (VM_USB)", QVariant::fromValue(static_cast<int>(ModuleListType::ReadoutStack)));
    ui->combo_listType->addItem("Stop DAQ", QVariant::fromValue(static_cast<int>(ModuleListType::StopDAQ)));

    ui->combo_listType->setCurrentIndex(2);

    connect(ui->combo_listType, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ModuleConfigDialog::handleListTypeIndexChanged);

    connect(ui->le_name, &QLineEdit::textChanged, this, [this](const QString &) {
            if (ui->le_name->hasAcceptableInput())
            {
                setModified(true);
            }
    });

    // VME base address
    ui->le_address->setInputMask("\\0\\xHHHH\\0\\0\\0\\0");

    connect(ui->le_address, &QLineEdit::textChanged, this, [this](const QString &) {
            if (ui->le_address->hasAcceptableInput())
            {
                setModified(true);
            }
    });

    // register list / stack editor
    connect(ui->editor, &QTextEdit::textChanged, this, [this] {
            setModified(true);
    });

    loadFromConfig();

    actLoadFile = new QAction("from file", this);
    actLoadTemplate = new QAction("from template", this);

    auto menu = new QMenu(ui->pb_load);
    menu->addAction(actLoadFile);
    menu->addAction(actLoadTemplate);
    ui->pb_load->setMenu(menu);

    connect(actLoadFile, &QAction::triggered, this, &ModuleConfigDialog::loadFromFile);
    connect(actLoadTemplate, &QAction::triggered, this, &ModuleConfigDialog::loadFromTemplate);
    connect(ui->pb_save, &QPushButton::clicked, this, &ModuleConfigDialog::saveToFile);
    connect(ui->pb_exec, &QPushButton::clicked, this, &ModuleConfigDialog::execList);
    connect(ui->pb_initModule, &QPushButton::clicked, this, &ModuleConfigDialog::initModule);

    ui->splitter->setSizes({1, 0});

    auto controller = m_context->getController();

    auto onControllerOpenChanged = [=] {
        bool open = controller->isOpen();
        ui->pb_exec->setEnabled(open);
        ui->pb_initModule->setEnabled(open);
        if (open)
        {
            ui->pb_exec->setToolTip(QSL(""));
            ui->pb_initModule->setToolTip(QSL(""));
        }
        else
        {
            ui->pb_exec->setToolTip(QSL("Controller not connected"));
            ui->pb_initModule->setToolTip(QSL("Controller not connected"));
        }
    };

    connect(controller, &VMEController::controllerOpened, this, onControllerOpenChanged);
    connect(controller, &VMEController::controllerClosed, this, onControllerOpenChanged);
    onControllerOpenChanged();
    handleListTypeIndexChanged(0);
}

ModuleConfigDialog::~ModuleConfigDialog()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void ModuleConfigDialog::handleListTypeIndexChanged(int index)
{
    if (ui->editor->document()->isModified())
    {
        auto lastType = ui->combo_listType->itemData(m_lastListTypeIndex).toInt();
        m_configStrings[lastType] = ui->editor->toPlainText();
        setModified(true);
    }

    m_lastListTypeIndex = index;

    auto currentType = ui->combo_listType->currentData().toInt();
    {
        QSignalBlocker b(ui->editor);
        ui->editor->setPlainText(m_configStrings.value(currentType));
    }
    ui->editor->document()->setModified(false);
    ui->editor->document()->clearUndoRedoStacks();

    switch (static_cast<ModuleListType>(currentType))
    {
        case ModuleListType::ReadoutStack:
            ui->pb_exec->setText("Exec");
            ui->pb_initModule->setVisible(true);
            ui->pb_load->setVisible(false);
            ui->pb_save->setVisible(false);
            ui->splitter->setSizes({1, 1});
            ui->editor->setReadOnly(true);
            break;

        default:
            ui->pb_exec->setText("Run");
            ui->pb_initModule->setVisible(false);
            ui->pb_load->setVisible(true);
            ui->pb_save->setVisible(true);
            ui->splitter->setSizes({1, 0});
            ui->editor->setReadOnly(false);
            break;
    }
}

void ModuleConfigDialog::closeEvent(QCloseEvent *event)
{
    if (m_hasModifications)
    {
        auto response = QMessageBox::question(this, QSL("Apply changes"),
                QSL("The module configuration was modified. Do you want to apply the changes?"),
                QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel);
        if (response == QMessageBox::Apply)
        {
            saveToConfig();
            event->accept();
            accept();
        }
        else if (response == QMessageBox::Discard)
        {
            event->accept();
            reject();
        }
        else
        {
            event->ignore();
        }
    }
    else
    {
        event->accept();
        accept();
    }
}

void ModuleConfigDialog::loadFromFile()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains("Files/LastInitListDirectory"))
    {
        path = settings.value("Files/LastInitListDirectory").toString();
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load init template", path,
                                                    "Init Lists (*.init);; All Files (*)");
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&file);
            ui->editor->setPlainText(stream.readAll());
            QFileInfo fi(fileName);
            settings.setValue("Files/LastInitListDirectory", fi.absolutePath());
        }
    }
}

void ModuleConfigDialog::loadFromTemplate()
{
    // TODO: This is duplicated in AddModuleDialog::accept(). Compress this!
    QStringList templatePaths;
    templatePaths << QDir::currentPath() + "/templates";
    templatePaths << QCoreApplication::applicationDirPath() + "/templates";

    QString templatePath;

    for (auto testPath: templatePaths)
    {
        if (QFileInfo(testPath).exists())
        {
            templatePath = testPath;
            break;
        }
    }


    if (templatePath.isEmpty())
    {
        QMessageBox::warning(this, QSL("Error"), QSL("No module template directory found."));
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(this, "Load init template", templatePath,
                                                    "Init Lists (*.init);; All Files (*)");

    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&file);
            ui->editor->setPlainText(stream.readAll());
        }
    }
}

void ModuleConfigDialog::saveToFile()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains("Files/LastInitListDirectory"))
    {
        path = settings.value("Files/LastInitListDirectory").toString();
    }

    QString fileName = QFileDialog::getSaveFileName(this, "Load init template", path,
                                                    "Init Lists (*.init);; All Files (*)");

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".init";
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "File error", QString("Error opening \"%1\" for writing").arg(fileName));
        return;
    }

    QTextStream stream(&file);
    stream << ui->editor->toPlainText();

    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::critical(this, "File error", QString("Error writing to \"%1\"").arg(fileName));
        return;
    }

    settings.setValue("Files/LastInitListDirectory", fi.absolutePath());
}

void ModuleConfigDialog::execList()
{
    auto controller = m_context->getController();

    if (controller && controller->isOpen())
    {
        auto type = static_cast<ModuleListType>(ui->combo_listType->currentData().toInt());
        QString listContents = ui->editor->toPlainText();

        switch (type)
        {
            case ModuleListType::Parameters:
            case ModuleListType::Readout:
            case ModuleListType::StartDAQ:
            case ModuleListType::StopDAQ:
            case ModuleListType::Reset:
                {
                    u8 ignored[100];
                    auto cmdList = VMECommandList::fromInitList(parseRegisterList(listContents),
                            m_config->baseAddress);

                    qDebug() << "command list:";
                    qDebug() << cmdList.toString();

                    ssize_t result = controller->executeCommands(&cmdList, ignored, sizeof(ignored));
                    if (result < 0)
                    {
                        QMessageBox::warning(this,
                                             "Error running commands",
                                             QString("Error running commands (code=%1")
                                             .arg(result));
                    }
                } break;
            case ModuleListType::ReadoutStack:
                {
                    auto vmusb = dynamic_cast<VMUSB *>(controller);
                    if (vmusb)
                    {
                        QVector<u32> result = vmusb->stackExecute(parseStackFile(listContents), 1<<16);
                        QString buf;
                        QTextStream stream(&buf);
                        for (int idx=0; idx<result.size(); ++idx)
                        {
                            u32 value = result[idx];

                            stream
                                << qSetFieldWidth(4) << qSetPadChar(' ') << dec << idx
                                << qSetFieldWidth(0) << ": 0x"
                                << hex << qSetFieldWidth(8) << qSetPadChar('0') << value
                                << qSetFieldWidth(0)
                                << endl;
                        }
                        ui->output->setPlainText(buf);
                        ui->splitter->setSizes({1, 1});
                    }
                } break;
            case ModuleListType::TypeCount:
                break;
        }
    }
}

void ModuleConfigDialog::initModule()
{
    RegisterList regs = parseRegisterList(m_configStrings[(int)ModuleListType::Reset]);
    auto controller = m_context->getController();
    controller->applyRegisterList(regs, m_config->baseAddress);
    QThread::msleep(500);

    regs = parseRegisterList(m_configStrings[(int)ModuleListType::Parameters]);
    regs += parseRegisterList(m_configStrings[(int)ModuleListType::Readout]);
    regs += parseRegisterList(m_configStrings[(int)ModuleListType::StartDAQ]);
    controller->applyRegisterList(regs, m_config->baseAddress);
}

void ModuleConfigDialog::setModified(bool modified)
{
    m_hasModifications = modified;
    ui->buttonBox->button(QDialogButtonBox::Reset)->setEnabled(modified);
    ui->buttonBox->button(QDialogButtonBox::Apply)->setEnabled(modified);
}

void ModuleConfigDialog::on_buttonBox_clicked(QAbstractButton *button)
{
    auto buttonRole = ui->buttonBox->buttonRole(button);

    switch (buttonRole)
    {
        case QDialogButtonBox::ApplyRole:
            {
                saveToConfig();
                loadFromConfig();
                setWindowTitle(QString("Module Config %1").arg(m_config->getName()));
            } break;

        case QDialogButtonBox::ResetRole:
            {
                loadFromConfig();
            } break;

        case QDialogButtonBox::RejectRole:
            {
                if (m_hasModifications)
                {
                    auto response = QMessageBox::question(this, QSL("Apply changes"),
                            QSL("The module configuration was modified. Do you want to apply the changes?"),
                            QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel);
                    if (response == QMessageBox::Apply)
                    {
                        saveToConfig();
                        accept();
                    }
                    else if (response == QMessageBox::Discard)
                    {
                        reject();
                    }
                }
                else
                {
                    reject();
                }
            } break;

        default:
            Q_ASSERT(false);
            break;
    }
}

void ModuleConfigDialog::loadFromConfig()
{
    auto config = m_config;

    setWindowTitle(QString("Module Config %1").arg(config->getName()));
    ui->label_type->setText(VMEModuleTypeNames.value(config->type, QSL("Unknown")));
    ui->le_name->setText(config->getName());
    ui->le_address->setText(QString().sprintf("0x%08x", config->baseAddress));

    m_configStrings.clear();

    for (int i=0; i < static_cast<int>(ModuleListType::TypeCount); ++i)
    {
        m_configStrings[i] = *getConfigString(static_cast<ModuleListType>(i), config);
    }

    auto currentType = ui->combo_listType->currentData().toInt();
    ui->editor->setPlainText(m_configStrings.value(currentType));
    ui->editor->document()->setModified(false);
    ui->editor->document()->clearUndoRedoStacks();
    setModified(false);
}

void ModuleConfigDialog::saveToConfig()
{
    auto config = m_config;

    config->setName(ui->le_name->text());

    bool ok;
    m_config->baseAddress = ui->le_address->text().toUInt(&ok, 16);

    auto currentType = ui->combo_listType->currentData().toInt();
    m_configStrings[currentType] = ui->editor->toPlainText();

    for (int i=0; i < static_cast<int>(ModuleListType::TypeCount); ++i)
    {
        *getConfigString(static_cast<ModuleListType>(i), config) = m_configStrings[i];
    }
    m_config->generateReadoutStack();
    setModified(false);
}
