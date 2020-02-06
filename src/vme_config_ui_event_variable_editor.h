#ifndef __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__
#define __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__

#include <memory>
#include <QWidget>

#include "libmvme_export.h"
#include "vme_config.h"
#include "vme_script.h"

class LIBMVME_EXPORT EventVariableEditor: public QWidget
{
    Q_OBJECT
    signals:
        void logMessage(const QString &str);
        void logError(const QString &str);

    public:
        using RunScriptCallback = std::function<
            vme_script::ResultList (
                const vme_script::VMEScript &,
                vme_script::LoggerFun)>;

        explicit EventVariableEditor(
            EventConfig *eventConfig,
            RunScriptCallback runScriptCallback,
            QWidget *parent = nullptr);

        ~EventVariableEditor();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__ */
