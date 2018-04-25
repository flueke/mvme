#ifndef __EXPRESSION_OPERATOR_DIALOG_H__
#define __EXPRESSION_OPERATOR_DIALOG_H__

#include <memory>
#include <QDialog>

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
        ExpressionOperatorDialog(ExpressionOperator *op, int userLevel,
                                 EventWidget *eventWidget, QWidget *parent = nullptr);
        virtual ~ExpressionOperatorDialog();

    private:
        struct Private;
        std::unique_ptr<Private> m_d;
};

} // end namespace analysis

#endif /* __EXPRESSION_OPERATOR_DIALOG_H__ */
