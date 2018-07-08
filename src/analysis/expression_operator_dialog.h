#ifndef __EXPRESSION_OPERATOR_DIALOG_H__
#define __EXPRESSION_OPERATOR_DIALOG_H__

#include <memory>
#include <QDialog>
#include "analysis_ui_p.h"
#include "object_editor_dialog.h"

class MVMEContext;

namespace analysis
{

class EventWidget;
class ExpressionOperator;

class ExpressionOperatorDialog: public ObjectEditorDialog
{
    Q_OBJECT
    signals:
        void applied();

    public:
    ExpressionOperatorDialog(const std::shared_ptr<ExpressionOperator> &op, int userLevel,
                             ObjectEditorMode mode, EventWidget *eventWidget);

        virtual ~ExpressionOperatorDialog();


    public slots:
        void apply();
        virtual void accept() override;
        virtual void reject() override;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // end namespace analysis

#endif /* __EXPRESSION_OPERATOR_DIALOG_H__ */
