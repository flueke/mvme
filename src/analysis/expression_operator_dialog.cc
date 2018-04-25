#include "expression_operator_dialog.h"

#include "analysis_ui_p.h"

/* NOTES:
 - new slot grid implementation with space for the input variable name column.
   2nd step: try to abstract this so the slot grid can be instantiated and customized and is reusable

 - workflow:
   select inputs, write init script, run init script, check output definition is as desired,
   write step script, test with sample data, accept changes

 - required:
   clickable error display for scripts. errors can come from expr_tk (wrapped
   in a2_exprtk layer) and from the ExpressionOperator itself (e.g. malformed beginExpr output, SemanticError).

 - utility: symbol table inspection


 Steps to be able to use the new widget and test the new components:
 * Refactor the existing AddEditOperatorWidget to not use the raw pointer vs.
   shared pointer hack to decide whether to add or to edit an operator. Figure
   out if this is needed at all or if this can be handled outside of the
   widget, e.g. by connecting a "accepted" signal to the correct slot depending
   on where the widget was instantiated.

 * make a factory function that returns the correct widget depending on the operator type.
   this will still sort of suck as something similar already happens in the
   current AddEditOperatorWidget to decide which OperatorConfigurationWidget to
   instantiate, but that's how it is.

 * Leave the logic for the current operators as is, i.e. make backups of
   current slot connections, modify the operator immediately on selecting a new
   input, etc.


 */


namespace
{
using namespace analysis;

struct SlotGrid
{
    QFrame *outerFrame,
           *slotFrame;

    QGridLayout *slotLayout;

    QPushButton *addSlotButton,
                *removeSlotButton;

    QVector<QPushButton *> selectButtons;
    QVector<QPushButton *> clearButtons;
};

SlotGrid make_slotgrid(QWidget *parent = nullptr)
{
    SlotGrid sg = {};

    sg.slotFrame  = new QFrame;
    sg.slotLayout = new QGridLayout(sg.slotFrame);

    sg.slotLayout->setContentsMargins(2, 2, 2, 2);
    sg.slotLayout->setColumnStretch(0, 0); // index
    sg.slotLayout->setColumnStretch(1, 1); // select button with input name
    sg.slotLayout->setColumnStretch(2, 1); // variable name inside the script
    sg.slotLayout->setColumnStretch(3, 0); // clear selection button


    sg.addSlotButton = new QPushButton(QIcon(QSL(":/list_add.png")), QString());
    sg.addSlotButton->setToolTip(QSL("Add input"));

    sg.removeSlotButton = new QPushButton(QIcon(QSL(":/list_remove.png")), QString());
    sg.removeSlotButton->setToolTip(QSL("Remove last input"));

    auto addRemoveSlotLayout = new QHBoxLayout;
    addRemoveSlotLayout->addWidget(sg.addSlotButton);
    addRemoveSlotLayout->addWidget(sg.removeSlotButton);

    sg.outerFrame = new QFrame(parent);
    auto outerLayout = new QVBoxLayout(sg.outerFrame);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(sg.slotFrame);
    outerLayout->addLayout(addRemoveSlotLayout);

    return sg;
}

#if 0
using SelectSlotCallback = std::function<void 

void repopulate_slotgrid(SlotGrid sg, const QVector<Slot *> &slots, 
{
}
#endif

} // end anon namespace

namespace analysis
{

struct ExpressionOperatorDialog::Private
{
    Private(ExpressionOperatorDialog *q)
        : m_q(q)
    {}

    ExpressionOperatorDialog *m_q;
    int m_userLevel;
    EventWidget *m_eventWidget;
};

ExpressionOperatorDialog::ExpressionOperatorDialog(ExpressionOperator *op,
                                                   int userLevel,
                                                   EventWidget *eventWidget,
                                                   QWidget *parent)
    : QDialog(parent)
    , m_d(std::make_unique<Private>(this))
{
    m_d->m_userLevel   = userLevel;
    m_d->m_eventWidget = eventWidget;
}

ExpressionOperatorDialog::~ExpressionOperatorDialog()
{
}

} // end namespace analysis
