#include "config_widgets.h"
#include "ui_moduleconfig_widget.h"
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
    ReadoutStack
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
    }

    return nullptr;
}

ModuleConfigWidget::ModuleConfigWidget(MVMEContext *context, ModuleConfig *config, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ModuleConfigWidget)
    , m_context(context)
    , m_config(config)
{
    ui->setupUi(this);
    setWindowTitle(QString("Module Config for %1").arg(m_config->getName()));

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
            this, &ModuleConfigWidget::handleListTypeIndexChanged);

    ui->label_type->setText(VMEModuleTypeNames[config->type]);

    ui->le_name->setText(config->getName());
    connect(ui->le_name, &QLineEdit::editingFinished, this, &ModuleConfigWidget::onNameEditFinished);

    ui->le_address->setInputMask("\\0\\xHHHH\\0\\0\\0\\0");
    ui->le_address->setText(QString().sprintf("0x%08x", config->baseAddress));
    connect(ui->le_address, &QLineEdit::editingFinished, this, &ModuleConfigWidget::onAddressEditFinished);

    ui->editor->setPlainText(*getConfigString(ModuleListType::Parameters, config));
    ui->editor->document()->setModified(false);

    connect(ui->editor->document(), &QTextDocument::contentsChanged,
            this, &ModuleConfigWidget::editorContentsChanged);

    actLoadFile = new QAction("from file", this);
    actLoadTemplate = new QAction("from template", this);

    auto menu = new QMenu(ui->pb_load);
    menu->addAction(actLoadFile);
    menu->addAction(actLoadTemplate);
    ui->pb_load->setMenu(menu);

    connect(actLoadFile, &QAction::triggered, this, &ModuleConfigWidget::loadFromFile);
    connect(actLoadTemplate, &QAction::triggered, this, &ModuleConfigWidget::loadFromTemplate);
    connect(ui->pb_save, &QPushButton::clicked, this, &ModuleConfigWidget::saveToFile);
    connect(ui->pb_exec, &QPushButton::clicked, this, &ModuleConfigWidget::execList);

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

void ModuleConfigWidget::handleListTypeIndexChanged(int index)
{
    if (m_lastListTypeIndex >= 0 && ui->editor->document()->isModified())
    {
        auto type = static_cast<ModuleListType>(ui->combo_listType->itemData(m_lastListTypeIndex).toInt());

        QString *dest = getConfigString(type, m_config);
        if (dest)
        {
            *dest = ui->editor->toPlainText();
        }
    }

    m_lastListTypeIndex = index;

    m_ignoreEditorContentsChange = true;
    ui->editor->clear();
    ui->editor->document()->clearUndoRedoStacks();
    auto type = static_cast<ModuleListType>(ui->combo_listType->itemData(index).toInt());
    QString *contents = getConfigString(type, m_config);
    if (contents)
    {
        ui->editor->setPlainText(*contents);
    }
    ui->editor->document()->setModified(false);
    m_ignoreEditorContentsChange = false;

    switch (type)
    {
        case ModuleListType::ReadoutStack:
            ui->pb_exec->setText("Exec");
            break;
        default:
            ui->pb_exec->setText("Run");
            break;
    }
}

void ModuleConfigWidget::editorContentsChanged()
{
    if (m_ignoreEditorContentsChange)
        return;

    auto type = static_cast<ModuleListType>(ui->combo_listType->itemData(m_lastListTypeIndex).toInt());
    QString *dest = getConfigString(type, m_config);
    if (dest)
    {
        *dest = ui->editor->toPlainText();
    }
}

void ModuleConfigWidget::onNameEditFinished()
{
    QString name = ui->le_name->text();
    if (ui->le_name->hasAcceptableInput() && name.size())
    {
        m_config->setName(name);
        m_context->getConfig()->setModified();
    }
    else
    {
        ui->le_name->setText(m_config->getName());
    }
}

void ModuleConfigWidget::onAddressEditFinished()
{
    if (ui->le_address->hasAcceptableInput())
    {
        bool ok;
        m_config->baseAddress = ui->le_address->text().toUInt(&ok, 16);
        m_context->getConfig()->setModified();
    }
    else
    {
        auto text = QString().sprintf("0x%08x", m_config->baseAddress);
        ui->le_address->setText(text);
    }
}

void ModuleConfigWidget::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
}

void ModuleConfigWidget::loadFromFile()
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

void ModuleConfigWidget::loadFromTemplate()
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

void ModuleConfigWidget::saveToFile()
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

void ModuleConfigWidget::execList()
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
        }
    }
}
