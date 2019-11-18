#ifndef __MVME_MVLC_TRIGGER_IO_EDITOR_H__
#define __MVME_MVLC_TRIGGER_IO_EDITOR_H__

#include <memory>
#include "libmvme_export.h"
#include "vme_config.h"

namespace mesytec
{

class LIBMVME_EXPORT MVLCTriggerIOEditor: public QWidget
{
    Q_OBJECT
    signals:
        void logMessage(const QString &msg);
        void runScriptConfig(VMEScriptConfig *setupScript);
        void addApplicationWidget(QWidget *widget);

    public:
        MVLCTriggerIOEditor(
            VMEScriptConfig *triggerIOScript,
            QWidget *parent = nullptr);

        ~MVLCTriggerIOEditor();

    public slots:
        // Set the event names from the vme config. This information is
        // displayed when editing one of the MVLC StackStart or StackBusy
        // units.
        void setVMEEventNames(const QStringList &names);

    private slots:
        void runScript_();
        void setupModified();
        void regenerateScript();
        void reload();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mesytec

#endif /* __MVME_MVLC_TRIGGER_IO_EDITOR_H__ */
