#ifndef __VME_SCRIPT_EDITOR_H__
#define __VME_SCRIPT_EDITOR_H__

#include "util.h"

class MVMEContext;
class VMEScriptConfig;
class QTextEdit;

class VMEScriptEditor: public MVMEWidget
{
    Q_OBJECT
    public:
        VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent = 0);

    private:
        void onScriptModified(bool isModified);

        void updateWindowTitle();


        MVMEContext *m_context;
        VMEScriptConfig *m_script;
        QTextEdit *m_editor;
};

#endif /* __VME_SCRIPT_EDITOR_H__ */
