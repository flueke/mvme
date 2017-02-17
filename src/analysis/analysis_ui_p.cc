#include "analysis_ui_p.h"

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
        selectButton->setCheckable(true);
        m_selectButtons.push_back(selectButton);

        connect(selectButton, &QPushButton::toggled, this, [this, slot, slotIndex, userLevel](bool checked) {
            // Cancel any previous input selection. Has no effect if no input selection was active.
            m_eventWidget->endSelectInput();

            if (checked)
            {
                for (s32 buttonIndex = 0; buttonIndex < m_selectButtons.size(); ++buttonIndex)
                {
                    if (buttonIndex != slotIndex)
                    {
                        m_selectButtons[buttonIndex]->setChecked(false);
                    }
                }

                /* Tell the EventWidget that we want input for the current slot.
                 * The lambda is the callback for the EventWidget. This means
                 * inputSelected() will be called with the current slotIndex
                 * once input selection is complete. */
                m_eventWidget->selectInputFor(slot, userLevel, [this, slotIndex] () {
                    this->inputSelected(slotIndex);
                });
            }

            m_inputSelectActive = checked;
        });

        auto clearButton  = new QPushButton(QIcon(":/dialog-close.png"), QString());

        connect(clearButton, &QPushButton::clicked, this, [this, slot, slotIndex]() {
            // End any active input selection if any of the clear buttons is
            // clicked. Seems to make the most sense in the UI.
            m_eventWidget->endSelectInput();

            for (auto button: m_selectButtons)
            {
                button->setChecked(false);
            }

            // Clear the slot
            slot->disconnectPipe();
            // Update the current select button to reflect the change
            m_selectButtons[slotIndex]->setText(QSL("<select>"));
            // Disable ok button as there's now at least one unset input
            m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        });

        slotGrid->addWidget(new QLabel(slot->name), row, 0);
        slotGrid->addWidget(selectButton, row, 1);
        slotGrid->addWidget(clearButton, row, 2);
        ++row;
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddOperatorWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    int col = 0, maxCol = 1;
    row = 0;
    // row, col, rowSpan, colSpan
    layout->addLayout(slotGrid, row++, 0);
    layout->addLayout(buttonBoxLayout, row++, 0);

    layout->setRowStretch(0, 1);
}

void AddOperatorWidget::inputSelected(s32 slotIndex)
{
    Slot *slot = m_op->getSlot(slotIndex);
    qDebug() << __PRETTY_FUNCTION__ << slot;

    auto selectButton = m_selectButtons[slotIndex];
    QSignalBlocker b(selectButton);
    selectButton->setChecked(false);

    QString buttonText = slot->inputPipe->source->objectName();
    if (slot->paramIndex != Slot::NoParamIndex)
    {
        buttonText = QString("%1[%2]").arg(buttonText).arg(slot->paramIndex);
    }
    else
    {
        buttonText = QString("%1 (size=%2)").arg(buttonText).arg(slot->inputPipe->getParameters().size());
    }
    selectButton->setText(buttonText);

    bool enableOkButton = true;

    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
    {
        if (!m_op->getSlot(slotIndex)->inputPipe)
        {
            enableOkButton = false;
            break;
        }
    }

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enableOkButton);
    m_inputSelectActive = false;
}

void AddOperatorWidget::accept()
{
    m_eventWidget->addOperator(m_op, m_userLevel);
    close();
}

void AddOperatorWidget::closeEvent(QCloseEvent *event)
{
    m_eventWidget->endSelectInput();
    m_eventWidget->addAnalysisElementWidgetCloses();
    event->accept();
}

}
