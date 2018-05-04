#ifndef __EXPRESSION_OPERATOR_DIALOG_H__
#define __EXPRESSION_OPERATOR_DIALOG_H__

#include <memory>
#include <QDialog>
#include "analysis_ui_p.h"

class MVMEContext;

namespace analysis
{

class EventWidget;
class ExpressionOperator;

class ExpressionOperatorDialog: public QDialog
{
    Q_OBJECT
    signals:
        void applied();

    public:
        ExpressionOperatorDialog(const std::shared_ptr<ExpressionOperator> &op, int userLevel,
                                 OperatorEditorMode mode, EventWidget *eventWidget);

        virtual ~ExpressionOperatorDialog();

        virtual void accept() override;
        virtual void reject() override;

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // end namespace analysis

#endif /* __EXPRESSION_OPERATOR_DIALOG_H__ */
