#ifndef __MVME_MVLC_TRIGGER_IO_EDITOR_H__
#define __MVME_MVLC_TRIGGER_IO_EDITOR_H__

#include <memory>
#include "vme_config.h"

namespace mesytec
{

class MVLCTriggerIOEditor: public QWidget
{
    Q_OBJECT
    signals:
        void logMessage(const QString &msg);
        void runScriptConfig(VMEScriptConfig *setupScript);
        void addApplicationWidget(QWidget *widget);

    public:
        MVLCTriggerIOEditor(VMEScriptConfig *setupScript, QWidget *parent = nullptr);
        ~MVLCTriggerIOEditor();

    private slots:
        void runScript_();
        void setupModified();
        void regenerateScript();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_EDITOR_H__ */
