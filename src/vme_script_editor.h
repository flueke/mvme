#ifndef __VME_SCRIPT_EDITOR_H__
#define __VME_SCRIPT_EDITOR_H__

#include "util.h"

class MVMEContext;
class VMEScriptConfig;

class QTextEdit;
class QToolBar;

class VMEScriptEditor: public MVMEWidget
{
    Q_OBJECT
    public:
        VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent = 0);

    private:
        void updateWindowTitle();

        void onEditorTextChanged();
        void onScriptModified(bool isModified);

        void runScript();
        void saveAndClose();
        void saveToConfig();
        void loadFromConfig();


        MVMEContext *m_context;
        VMEScriptConfig *m_script;

        QToolBar *m_toolbar;
        QTextEdit *m_editor;
};

#endif /* __VME_SCRIPT_EDITOR_H__ */
