#include "analysis/condition_ui_p.h"

#include <QHeaderView>

#include "mvme_context.h"
#include "mvme_context_lib.h"

namespace analysis
{
namespace ui
{


ConditionIntervalEditor::ConditionIntervalEditor(ConditionInterval *cond,
                                                 MVMEContext *context,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_cond(cond)
    , m_context(context)
{
    le_name = new QLineEdit(this);
    le_name->setText(cond->objectName());

    auto topLayout = new QFormLayout;
    topLayout->setContentsMargins(2, 2, 2, 2);
    topLayout->addRow(QSL("Name"), le_name);

    m_table = new QTableWidget(this);
    //m_table->setMinimumSize(325, 175);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({"Address", "Min", "Max"});
    m_table->verticalHeader()->setVisible(false);

    m_bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto bbLayout = new QHBoxLayout;
    bbLayout->addStretch(1);
    bbLayout->addWidget(m_bb);

    QObject::connect(m_bb, &QDialogButtonBox::accepted, this, &ConditionIntervalEditor::accept);
    QObject::connect(m_bb, &QDialogButtonBox::rejected, this, &ConditionIntervalEditor::reject);

    auto layout = new QVBoxLayout(this);
    layout->addLayout(topLayout);
    layout->addWidget(m_table);
    layout->addLayout(bbLayout);
    layout->setStretch(1, 1);

    setWindowTitle(QSL("Edit Interval Cut"));

    // populate the table
    auto intervals = cond->getIntervals();

    m_table->setRowCount(intervals.size());

    for (s32 addr = 0; addr < intervals.size(); addr++)
    {
        auto interval = intervals[addr];

        // col0: address
        auto item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsEnabled);
        item->setData(Qt::DisplayRole, addr);
        m_table->setItem(addr, 0, item);

        // col1: min
        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, interval.minValue());
        m_table->setItem(addr, 1, item);

        // col2: max
        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, interval.maxValue());
        m_table->setItem(addr, 2, item);
    }

    m_table->resizeRowsToContents();

    resize(325, 400);
}

ConditionIntervalEditor::~ConditionIntervalEditor()
{
}

void ConditionIntervalEditor::accept()
{
    if (le_name->text() != m_cond->objectName())
    {
        m_cond->setObjectName(le_name->text());
        m_context->getAnalysis()->setModified(true);
    }

    QVector<QwtInterval> intervals;
    intervals.reserve(m_table->rowCount());

    for (s32 addr = 0; addr < m_table->rowCount(); addr++)
    {
        double minValue = m_table->item(addr, 1)->data(Qt::EditRole).toDouble();
        double maxValue = m_table->item(addr, 2)->data(Qt::EditRole).toDouble();
        qDebug() << __PRETTY_FUNCTION__ << addr << minValue << maxValue;
        QwtInterval interval { minValue, maxValue };
        interval = interval.normalized();
        intervals.push_back(interval);
    }

    if (m_cond->getIntervals() != intervals)
    {
        AnalysisPauser pauser(m_context);

        m_cond->setIntervals(intervals);

        auto op = std::dynamic_pointer_cast<OperatorInterface>(m_cond->shared_from_this());
        m_context->getAnalysis()->setOperatorEdited(op);
    }

    QDialog::accept();
}

void ConditionIntervalEditor::reject()
{
    QDialog::reject();
}

} // end namespace ui
} // end namespace analysis
