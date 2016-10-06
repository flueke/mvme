#include "vme_script_editor.h"
#include "mvme_context.h"
#include "vme_script.h"
#include <QTextEdit>

VMEScriptEditor::VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script,
                                 uint32_t baseAddress, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_script(script)
    , m_baseAddress(baseAddress)
    , m_textEdit(new QTextEdit(this))
{
}
