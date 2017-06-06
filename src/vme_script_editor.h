#ifndef __VME_SCRIPT_EDITOR_H__
#define __VME_SCRIPT_EDITOR_H__

#include "util.h"

class QCloseEvent;

class MVMEContext;
class VMEScriptConfig;
class VMEScriptEditorPrivate;

class VMEScriptEditor: public MVMEWidget
{
    Q_OBJECT
    public:
        VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script, QWidget *parent = 0);
        ~VMEScriptEditor();

        bool isModified() const;
        void applyChanges() { apply(); }

        virtual bool event(QEvent *event) override;

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

        friend class VMEScriptEditorPrivate;
        VMEScriptEditorPrivate *m_d;
};

#endif /* __VME_SCRIPT_EDITOR_H__ */
