#include "vme_script_editor.h"
#include "mvme_context.h"
#include "vme_script.h"

#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

static const int tabStop = 4;

using namespace std::placeholders;

VMEScriptEditor::VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent)
    : MVMEWidget(parent)
    , m_context(context)
    , m_scriptConfig(script)
    , m_toolbar(new QToolBar)
    , m_editor(new QTextEdit)
{
    new vme_script::SyntaxHighlighter(m_editor);

    auto font = QFont("Monospace", 8);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    m_editor->setFont(font);

    {
        QString spaces;
        for (int i = 0; i < tabStop; ++i)
            spaces += " ";
        QFontMetrics metrics(font);
        m_editor->setTabStopWidth(metrics.width(spaces));
    }

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(m_toolbar);
    layout->addWidget(m_editor);

    connect(script, &VMEScriptConfig::modified, this, &VMEScriptEditor::onScriptModified);

    auto parentConfig = qobject_cast<ConfigObject *>(m_scriptConfig->parent());

    if (parentConfig)
        connect(parentConfig, &ConfigObject::modified, this, &VMEScriptEditor::updateWindowTitle);

    m_editor->setPlainText(m_scriptConfig->getScriptContents());
    updateWindowTitle();

    connect(m_editor->document(), &QTextDocument::contentsChanged, this, &VMEScriptEditor::onEditorTextChanged);

    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    m_toolbar->addAction(QIcon(":/script-run.png"), QSL("Run"), this,  &VMEScriptEditor::runScript);
    m_toolbar->addAction(QIcon(":/dialog-ok-apply.png"), QSL("Apply"), this, &VMEScriptEditor::apply);
    m_toolbar->addAction(QIcon(":/dialog-close.png"), QSL("Close"), this, &VMEScriptEditor::close);

    m_toolbar->addSeparator();

    auto loadMenu = new QMenu;
    loadMenu->addAction(QSL("from file"), this, &VMEScriptEditor::loadFromFile);
    loadMenu->addAction(QSL("from template"), this, &VMEScriptEditor::loadFromTemplate);
    auto loadAction = m_toolbar->addAction(QIcon(":/document-open.png"), QSL("Load"));
    loadAction->setMenu(loadMenu);
    
    auto loadButton = qobject_cast<QToolButton *>(m_toolbar->widgetForAction(loadAction));
    if (loadButton)
        loadButton->setPopupMode(QToolButton::InstantPopup);

    m_toolbar->addAction(QIcon(":/document-save-as.png"), "Save to file", this, &VMEScriptEditor::saveToFile);

    m_toolbar->addSeparator();

    m_toolbar->addAction(QIcon(":/document-revert.png"), "Revert Changes", this, &VMEScriptEditor::revert); 
}

void VMEScriptEditor::updateWindowTitle()
{
    auto title = get_title(m_scriptConfig);

    if (m_editor->document()->isModified())
        title += QSL(" *");

    setWindowTitle(title);
}

void VMEScriptEditor::onScriptModified(bool isModified)
{
    if (!isModified)
        return;

    // TODO: ask about reloading from the config or keeping the current text editor content
    //m_editor->setText(m_scriptConfig->getScriptContents());

    updateWindowTitle();
}

void VMEScriptEditor::onEditorTextChanged()
{
    updateWindowTitle();
}

void VMEScriptEditor::runScript()
{
    auto logger = std::bind(&MVMEContext::logMessage, m_context, _1);
    try
    {
        auto moduleConfig = qobject_cast<ModuleConfig *>(m_scriptConfig->parent());
        auto script = vme_script::parse(m_editor->toPlainText(),
                                        moduleConfig ? moduleConfig->getBaseAddress() : 0);
        auto results = vme_script::run_script(m_context->getController(), script, logger);

        for (auto result: results)
            logger(format_result(result));
    }
    catch (const vme_script::ParseError &e)
    {
        logger(QSL("Parse error: ") + e.what());
    }
}

void VMEScriptEditor::loadFromFile()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains("Files/LastVMEScriptDirectory"))
    {
        path = settings.value("Files/LastVMEScriptDirectory").toString();
    }

    QString fileName = QFileDialog::getOpenFileName(this, QSL("Load vme script file"), path,
                                                    QSL("VME scripts (*.vme);; All Files (*)"));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly))
        {
            QTextStream stream(&file);
            m_editor->setPlainText(stream.readAll());
            QFileInfo fi(fileName);
            settings.setValue("Files/LastVMEScriptDirectory", fi.absolutePath());
        }
    }
}

void VMEScriptEditor::loadFromTemplate()
{
    TemplateLoader loader;
    connect(&loader, &TemplateLoader::logMessage, m_context, &MVMEContext::logMessage);
    QString path = loader.getTemplatePath();

    if (!path.isEmpty())
    {
        QString fileName = QFileDialog::getOpenFileName(this, QSL("Load vme script file"), path,
                                                        QSL("VME scripts (*.vme);; All Files (*)"));
        if (!fileName.isEmpty())
        {
            QFile file(fileName);
            if (file.open(QIODevice::ReadOnly))
            {
                QTextStream stream(&file);
                m_editor->setPlainText(stream.readAll());
            }
        }
    }
}

void VMEScriptEditor::saveToFile()
{
    QString path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    QSettings settings;
    if (settings.contains("Files/LastVMEScriptDirectory"))
    {
        path = settings.value("Files/LastVMEScriptDirectory").toString();
    }

    QString fileName = QFileDialog::getSaveFileName(this, QSL("Save vme script file"), path,
                                                    QSL("VME scripts (*.vme);; All Files (*)"));

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".vme";
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "File error", QString("Error opening \"%1\" for writing").arg(fileName));
        return;
    }

    QTextStream stream(&file);
    stream << m_editor->toPlainText();

    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::critical(this, "File error", QString("Error writing to \"%1\"").arg(fileName));
        return;
    }

    settings.setValue("Files/LastVMEScriptDirectory", fi.absolutePath());
}

void VMEScriptEditor::apply()
{
    auto contents = m_editor->toPlainText();
    m_scriptConfig->setScriptContents(contents);
    m_editor->document()->setModified(false);
    updateWindowTitle();
}

void VMEScriptEditor::revert()
{
    m_editor->setPlainText(m_scriptConfig->getScriptContents());
    m_editor->document()->setModified(false);
    updateWindowTitle();
}

void VMEScriptEditor::closeEvent(QCloseEvent *event)
{
    bool doClose = !m_editor->document()->isModified();

    if (m_editor->document()->isModified())
    {
        auto response = QMessageBox::question(this, QSL("Apply changes?"),
                                              QSL("The script was modified. Do you want to apply the changes?"),
                                              QMessageBox::Apply | QMessageBox::Discard | QMessageBox::Cancel);

        if (response == QMessageBox::Apply)
        {
            apply();
            doClose = true;
        }
        else if (response == QMessageBox::Discard)
        {
            doClose = true;
        }
    }

    if (doClose)
        MVMEWidget::closeEvent(event);
    else
        event->ignore();
}
