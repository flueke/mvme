#ifndef __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__
#define __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__

#include <memory>
#include <QWidget>

#include "libmvme_export.h"
#include "vme_config.h"

class LIBMVME_EXPORT EventVariableEditor: public QWidget
{
    Q_OBJECT
    public:
        explicit EventVariableEditor(EventConfig *eventConfig, QWidget *parent = nullptr);
        ~EventVariableEditor();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif /* __MVME_VME_CONFIG_UI_EVENT_VARIABLE_EDITOR_H__ */
