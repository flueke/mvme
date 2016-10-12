#include "vme_script_editor.h"
#include "mvme_context.h"
#include "vme_script.h"

#include <QTextEdit>
#include <QVBoxLayout>
#include <QToolBar>

static const int tabStop = 4;

VMEScriptEditor::VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent)
    : MVMEWidget(parent)
    , m_context(context)
    , m_script(script)
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

    layout->addWidget(m_editor);
    layout->addWidget(m_toolbar);

    setMinimumWidth(350);

    connect(script, &VMEScriptConfig::modified, this, &VMEScriptEditor::onScriptModified);

    auto parentConfig = qobject_cast<ConfigObject *>(m_script->parent());

    if (parentConfig)
        connect(parentConfig, &ConfigObject::modified, this, &VMEScriptEditor::updateWindowTitle);

    m_editor->setPlainText(m_script->getScriptContents());
    updateWindowTitle();

    connect(m_editor->document(), &QTextDocument::contentsChanged, this, &VMEScriptEditor::onEditorTextChanged);

    m_toolbar->addAction("Run", this,  &VMEScriptEditor::runScript);
    m_toolbar->addAction("Save");
    m_toolbar->addAction("Save && Close"); 
    m_toolbar->addSeparator();
    m_toolbar->addAction("Revert Changes"); 
}

void VMEScriptEditor::updateWindowTitle()
{
    auto module     = qobject_cast<ModuleConfig *>(m_script->parent());
    auto event      = qobject_cast<EventConfig *>(m_script->parent());
    auto daqConfig  = qobject_cast<DAQConfig *>(m_script->parent());
    QString title;

    if (module)
    {
        title = QString("%1 for %2")
            .arg(m_script->objectName())
            .arg(module->objectName());
    }
    else if (event)
    {
        title = QString("%1 for %2")
            .arg(m_script->objectName())
            .arg(event->objectName());
    }
    else if (daqConfig)
    {
        title = QString("Global Script %2")
            .arg(m_script->objectName())
            ;
    }
    else
    {
        title = QString("VMEScript %1").arg(m_script->objectName());
    }

    if (m_editor->document()->isModified())
        title += QSL(" *");

    setWindowTitle(title);
}

void VMEScriptEditor::onScriptModified(bool isModified)
{
    if (!isModified)
        return;

    // TODO: ask about reloading from the config or keeping the current text editor content
    //m_editor->setText(m_script->getScriptContents());

    updateWindowTitle();
}

void VMEScriptEditor::onEditorTextChanged()
{
    updateWindowTitle();
}

void VMEScriptEditor::runScript()
{
}

void VMEScriptEditor::saveAndClose()
{
}

void VMEScriptEditor::saveToConfig()
{
}

void VMEScriptEditor::loadFromConfig()
{
}
