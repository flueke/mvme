#include "analysis_ui_p.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>

namespace analysis
{

AddOperatorWidget::AddOperatorWidget(OperatorPtr op, QWidget *parent, Qt::WindowFlags f)
    : QWidget(parent, f)
    , m_op(op)
{
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    int col = 0, maxCol = 1;
    int row = 0;

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    // row, col, rowSpan, colSpan
    layout->addWidget(buttons, row, 0, 1, maxCol);
}

}
