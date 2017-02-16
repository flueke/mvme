#include "analysis_ui_p.h"

#include <QPushButton>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>

namespace analysis
{

AddOperatorWidget::AddOperatorWidget(OperatorPtr op, s32 userLevel, EventWidget *eventWidget)
    : QWidget(eventWidget, Qt::Tool)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_eventWidget(eventWidget)
{
    auto slotGrid = new QGridLayout;
    int row = 0;
    for (s32 slotIndex = 0; slotIndex < op->getNumberOfSlots(); ++slotIndex)
    {
        Slot *slot = op->getSlot(slotIndex);
        
        auto selectButton = new QPushButton(QSL("<select>"));
        auto clearButton  = new QPushButton(QIcon(":/dialog-close.png"), QString());

        connect(selectButton, &QPushButton::clicked, this, [this, slot, userLevel]() {
            // tell the eventwidget that we want inputs for the current slot
            m_eventWidget->selectInputFor(slot, userLevel);
        });

        connect(clearButton, &QPushButton::clicked, this, [this]() {
        });

        slotGrid->addWidget(new QLabel(slot->name), row, 0);
        slotGrid->addWidget(selectButton, row, 1);
        slotGrid->addWidget(clearButton, row, 2);
        ++row;
    }

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    int col = 0, maxCol = 1;
    row = 0;
    // row, col, rowSpan, colSpan
    layout->addLayout(slotGrid, row++, 0);
    layout->addWidget(buttons, row++, 0, 1, maxCol);
}

}
