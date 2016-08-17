#include "config_widgets.h"
#include "ui_module_config_dialog.h"
#include "mvme_config.h"
#include "mvme_context.h"
#include "vmusb.h"

#include <QMenu>
#include <QStandardPaths>
#include <QSettings>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>

EventConfigWidget::EventConfigWidget(EventConfig *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
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
            auto pw = parentWidget();
            if (pw)
                pw->close();
            close();
        }
    });

    ui->combo_listType->addItem("Module Init", QVariant::fromValue(static_cast<int>(ModuleListType::Parameters)));
    ui->combo_listType->addItem("Readout Settings", QVariant::fromValue(static_cast<int>(ModuleListType::Readout)));
    ui->combo_listType->addItem("Readout Stack (VM_USB)", QVariant::fromValue(static_cast<int>(ModuleListType::ReadoutStack)));
    ui->combo_listType->addItem("Start DAQ", QVariant::fromValue(static_cast<int>(ModuleListType::StartDAQ)));
    ui->combo_listType->addItem("Stop DAQ", QVariant::fromValue(static_cast<int>(ModuleListType::StopDAQ)));
    ui->combo_listType->addItem("Module Reset", QVariant::fromValue(static_cast<int>(ModuleListType::Reset)));

    connect(ui->combo_listType, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ModuleConfigDialog::handleListTypeIndexChanged);

    ui->le_address->setInputMask("\\0\\xHHHH\\0\\0\\0\\0");

    loadFromConfig();


    connect(ui->editor->document(), &QTextDocument::contentsChanged,
            this, &ModuleConfigDialog::editorContentsChanged);

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

    ui->splitter->setSizes({1, 0});

    auto controller = m_context->getController();

    connect(controller, &VMEController::controllerOpened, this, [=] {
        ui->pb_exec->setEnabled(true);
    });

    connect(controller, &VMEController::controllerClosed, this, [=] {
        ui->pb_exec->setEnabled(false);
    });

    ui->pb_exec->setEnabled(controller->isOpen());
}

ModuleConfigDialog::~ModuleConfigDialog()
{
    qDebug() << __PRETTY_FUNCTION__;
}

void ModuleConfigDialog::handleListTypeIndexChanged(int index)
{
    if (m_lastListTypeIndex >= 0 && ui->editor->document()->isModified())
    {
        auto lastType = ui->combo_listType->itemData(m_lastListTypeIndex).toInt();
        m_configStrings[lastType] = ui->editor->toPlainText();
    }

    m_lastListTypeIndex = index;



    auto currentType = ui->combo_listType->currentData().toInt();
    m_ignoreEditorContentsChange = true;
    ui->editor->setPlainText(m_configStrings.value(currentType));
    ui->editor->document()->setModified(false);
    ui->editor->document()->clearUndoRedoStacks();
    m_ignoreEditorContentsChange = false;

    switch (static_cast<ModuleListType>(currentType))
    {
        case ModuleListType::ReadoutStack:
            ui->pb_exec->setText("Exec");
            ui->splitter->setSizes({1, 1});
            break;
        default:
            ui->pb_exec->setText("Run");
            ui->splitter->setSizes({1, 0});
            break;
    }
}

void ModuleConfigDialog::editorContentsChanged()
{
}

void ModuleConfigDialog::closeEvent(QCloseEvent *event)
{
    reject();
    QWidget::closeEvent(event);
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
    QString templatePath = QCoreApplication::applicationDirPath() + "/templates";
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
                    auto cmdList = VMECommandList::fromInitList(parseRegisterList(listContents), m_config->baseAddress);
                    controller->executeCommands(&cmdList, ignored, sizeof(ignored));
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

void ModuleConfigDialog::on_buttonBox_clicked(QAbstractButton *button)
{
    auto buttonRole = ui->buttonBox->buttonRole(button);

    switch (buttonRole)
    {
        case QDialogButtonBox::ApplyRole:
            {
                saveToConfig();
            } break;

        case QDialogButtonBox::ResetRole:
            {
                loadFromConfig();
            } break;

        case QDialogButtonBox::RejectRole:
            {
                reject();
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
    m_ignoreEditorContentsChange = true;
    ui->editor->setPlainText(m_configStrings.value(currentType));
    ui->editor->document()->setModified(false);
    ui->editor->document()->clearUndoRedoStacks();
    m_ignoreEditorContentsChange = false;
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
}
