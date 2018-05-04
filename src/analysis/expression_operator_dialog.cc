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
    QVector<QLineEdit *> varNameLineEdits;
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

    auto addRemoveSlotButtonsLayout = new QHBoxLayout;
    addRemoveSlotButtonsLayout->setContentsMargins(2, 2, 2, 2);
    addRemoveSlotButtonsLayout->addStretch();
    addRemoveSlotButtonsLayout->addWidget(sg.addSlotButton);
    addRemoveSlotButtonsLayout->addWidget(sg.removeSlotButton);

    sg.outerFrame = new QFrame(parent);
    auto outerLayout = new QVBoxLayout(sg.outerFrame);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(sg.slotFrame);
    outerLayout->addLayout(addRemoveSlotButtonsLayout);
    outerLayout->setStretch(0, 1);

    return sg;
}

void repopulate_slotgrid(SlotGrid &sg, OperatorInterface *op)
{
    // Clear the slot grid and the select buttons

    while (QLayoutItem *child = sg.slotLayout->takeAt(0))
    {
        if (auto widget = child->widget())
            delete widget;
        delete child;
    }

    Q_ASSERT(sg.slotLayout->count() == 0);

    // These have been deleted by the layout clearing code above.
    sg.selectButtons.clear();
    sg.varNameLineEdits.clear();

    // Repopulate

    const s32 slotCount = op->getNumberOfSlots();

    s32 row = 0;
    s32 col = 0;

    sg.slotLayout->addWidget(new QLabel(QSL("Input#")), row, col++);
    sg.slotLayout->addWidget(new QLabel(QSL("Variable Name")), row, col++);
    sg.slotLayout->setRowStretch(row, 0);

    row++;

    for (s32 slotIndex = 0; slotIndex < slotCount; slotIndex++)
    {
        Slot *slot = op->getSlot(slotIndex);

        auto selectButton = new QPushButton(QSL("<select>"));
        selectButton->setCheckable(true);
        selectButton->setMouseTracking(true);
        //XXXselectButton->installEventFilter(this);
        sg.selectButtons.push_back(selectButton);

        auto clearButton = new QPushButton(QIcon(":/dialog-close.png"), QString());

        auto le_varName  = new QLineEdit;
        sg.varNameLineEdits.push_back(le_varName);

        col = 0;

        sg.slotLayout->addWidget(new QLabel(slot->name), row, col++);
        sg.slotLayout->addWidget(selectButton, row, col++);
        sg.slotLayout->addWidget(clearButton, row, col++);
        sg.slotLayout->addWidget(le_varName, row, col++);

        row++;
    }

    sg.slotLayout->setRowStretch(row, 1);

    sg.slotLayout->setColumnStretch(0, 0);
    sg.slotLayout->setColumnStretch(1, 1);
    sg.slotLayout->setColumnStretch(2, 0);
    sg.slotLayout->setColumnStretch(3, 1);
}

void repopulate_slotgrid(SlotGrid &sg, const std::shared_ptr<OperatorInterface> &op)
{
    return repopulate_slotgrid(sg, op.get());
}

} // end anon namespace

namespace analysis
{

struct ExpressionOperatorDialog::Private
{
    Private(ExpressionOperatorDialog *q)
        : m_q(q)
    {}

    ExpressionOperatorDialog *m_q;
    std::shared_ptr<ExpressionOperator> m_op;
    int m_userLevel;
    OperatorEditorMode m_mode;
    EventWidget *m_eventWidget;
    SlotGrid m_slotGrid;

    QDialogButtonBox *m_buttonBox;

    void repopulateSlotGrid();
};

void ExpressionOperatorDialog::Private::repopulateSlotGrid()
{
    repopulate_slotgrid(m_slotGrid, m_op);
}

ExpressionOperatorDialog::ExpressionOperatorDialog(const std::shared_ptr<ExpressionOperator> &op, int userLevel,
                                                   OperatorEditorMode mode, EventWidget *eventWidget)
    : QDialog(eventWidget)
    , m_d(std::make_unique<Private>(this))
{
    m_d->m_op          = op;
    m_d->m_userLevel   = userLevel;
    m_d->m_mode        = mode;
    m_d->m_eventWidget = eventWidget;
    m_d->m_slotGrid    = make_slotgrid(this);

    add_widget_close_action(this);

    auto gb_slotGrid = new QGroupBox(QSL("Inputs"), this);
    auto gb_slotGridLayout = new QHBoxLayout(gb_slotGrid);
    gb_slotGridLayout->setContentsMargins(2, 2, 2, 2);
    gb_slotGridLayout->addWidget(m_d->m_slotGrid.outerFrame);

    m_d->m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_d->m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_d->m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

    connect(m_d->m_buttonBox, &QDialogButtonBox::accepted, this, &ExpressionOperatorDialog::accept);
    connect(m_d->m_buttonBox, &QDialogButtonBox::rejected, this, &ExpressionOperatorDialog::reject);

    auto dialogLayout = new QVBoxLayout(this);
    dialogLayout->addWidget(gb_slotGrid);
    dialogLayout->addWidget(m_d->m_buttonBox);
    dialogLayout->setStretch(0, 1);

    switch (m_d->m_mode)
    {
        case OperatorEditorMode::New:
            {
                setWindowTitle(QString("New  %1").arg(m_d->m_op->getDisplayName()));
            } break;
        case OperatorEditorMode::Edit:
            {
                setWindowTitle(QString("Edit %1").arg(m_d->m_op->getDisplayName()));
            } break;
    }

    connect(m_d->m_slotGrid.addSlotButton, &QPushButton::clicked, this, [this]() {
        m_d->m_op->addSlot();
        m_d->repopulateSlotGrid();
        //XXX endInputSelect
        m_d->m_slotGrid.removeSlotButton->setEnabled(m_d->m_op->getNumberOfSlots() > 1);
    });

    connect(m_d->m_slotGrid.removeSlotButton, &QPushButton::clicked, this, [this] () {
        if (m_d->m_op->getNumberOfSlots() > 1)
        {
            m_d->m_op->removeLastSlot();
            m_d->repopulateSlotGrid();
            //XXX endInputSelect();
        }
        m_d->m_slotGrid.removeSlotButton->setEnabled(m_d->m_op->getNumberOfSlots() > 1);
    });

    m_d->repopulateSlotGrid();
}

ExpressionOperatorDialog::~ExpressionOperatorDialog()
{
}

void ExpressionOperatorDialog::accept()
{
    QDialog::accept();
}

void ExpressionOperatorDialog::reject()
{
    QDialog::reject();
}

} // end namespace analysis
