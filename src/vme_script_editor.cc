#include "vme_script_editor.h"
#include "mvme_context.h"
#include "vme_script.h"

#include <QTextEdit>
#include <QVBoxLayout>
#include <QToolBar>

VMEScriptEditor::VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent)
    : MVMEWidget(parent)
    , m_context(context)
    , m_script(script)
    , m_editor(new QTextEdit(this))
{
    new vme_script::SyntaxHighlighter(m_editor);
    auto font = QFont("Monospace", 8);
    m_editor->setFont(font);


    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QToolBar);
    layout->addWidget(m_editor);

    setMinimumWidth(350);

    connect(script, &VMEScriptConfig::modified, this, &VMEScriptEditor::onScriptModified);
    onScriptModified(true);

    auto parentConfig = qobject_cast<ConfigObject *>(m_script->parent());

    if (parentConfig)
        connect(parentConfig, &ConfigObject::modified, this, &VMEScriptEditor::updateWindowTitle);
}

void VMEScriptEditor::onScriptModified(bool isModified)
{
    if (!isModified)
        return;

    m_editor->setText(m_script->getScriptContents());

    updateWindowTitle();
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

    setWindowTitle(title);
}
