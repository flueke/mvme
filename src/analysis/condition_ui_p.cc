/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "analysis/condition_ui_p.h"

#include <QHeaderView>

#include "mvme_context.h"
#include "mvme_context_lib.h"

namespace analysis
{
namespace ui
{


IntervalConditionEditor::IntervalConditionEditor(IntervalCondition *cond,
                                                 AnalysisServiceProvider *asp,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_cond(cond)
    , m_asp(asp)
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

    QObject::connect(m_bb, &QDialogButtonBox::accepted, this, &IntervalConditionEditor::accept);
    QObject::connect(m_bb, &QDialogButtonBox::rejected, this, &IntervalConditionEditor::reject);

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

IntervalConditionEditor::~IntervalConditionEditor()
{
}

void IntervalConditionEditor::accept()
{
    if (le_name->text() != m_cond->objectName())
    {
        m_cond->setObjectName(le_name->text());
        m_asp->getAnalysis()->setModified(true);
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
        AnalysisPauser pauser(m_asp);

        m_cond->setIntervals(intervals);

        auto op = std::dynamic_pointer_cast<OperatorInterface>(m_cond->shared_from_this());
        m_asp->getAnalysis()->setOperatorEdited(op);
    }

    QDialog::accept();
}

void IntervalConditionEditor::reject()
{
    QDialog::reject();
}

} // end namespace ui
} // end namespace analysis
