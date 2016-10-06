#ifndef __VME_SCRIPT_EDITOR_H__
#define __VME_SCRIPT_EDITOR_H__

#include <QWidget>

class MVMEContext;
class VMEScriptConfig;
class QTextEdit;

class VMEScriptEditor: public QWidget
{
    Q_OBJECT
    public:
        VMEScriptEditor(MVMEContext *context, VMEScriptConfig *script,
                        uint32_t baseAddress = 0, QWidget *parent = 0);

    private:
        MVMEContext *m_context;
        VMEScriptConfig *m_script;
        uint32_t m_baseAddress;

        QTextEdit *m_textEdit;
};

#endif /* __VME_SCRIPT_EDITOR_H__ */
