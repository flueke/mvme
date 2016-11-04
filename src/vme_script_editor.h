#ifndef __VME_SCRIPT_EDITOR_H__
#define __VME_SCRIPT_EDITOR_H__

#include "util.h"

class MVMEContext;
class VMEScriptConfig;

class QTextEdit;
class QToolBar;
class QCloseEvent;

class VMEScriptEditor: public MVMEWidget
{
    Q_OBJECT
    public:
        VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent = 0);

        bool isModified() const;
        void applyChanges() { apply(); }

    private:
        void updateWindowTitle();

        void onEditorTextChanged();
        void onScriptModified(bool isModified);

        void runScript();
        void loadFromFile();
        void loadFromTemplate();
        void saveToFile();
        void apply();
        void revert();

        virtual void closeEvent(QCloseEvent *event);


        MVMEContext *m_context;
        VMEScriptConfig *m_scriptConfig;

        QToolBar *m_toolbar;
        QTextEdit *m_editor;
};

#endif /* __VME_SCRIPT_EDITOR_H__ */
