/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "data_extraction_widget.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "data_filter_edit.h"
#include "qt_util.h"

namespace analysis
{
namespace ui
{

static const char *defaultNewFilter = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

DataExtractionEditor::DataExtractionEditor(QWidget *parent)
    : DataExtractionEditor(QVector<DataFilter>(), parent)
{}

DataExtractionEditor::DataExtractionEditor(const QVector<DataFilter> &subFilters, QWidget *parent)
    : QWidget(parent)
    , m_subFilters(subFilters)
{
    if (m_subFilters.isEmpty())
    {
        // Ensure we have at least one filter, otherwise the display would be
        // empty.
        m_subFilters.push_back(make_filter(defaultNewFilter));
    }


    setWindowTitle(QSL("Data Extraction"));

    auto filterGridWidget = new QWidget;
    m_filterGrid = new QGridLayout(filterGridWidget);
    m_filterGrid->setSpacing(6);
    m_filterGrid->setColumnStretch(1, 1);

    auto filterGridScrollArea = new QScrollArea;
    filterGridScrollArea->setWidget(filterGridWidget);
    filterGridScrollArea->setWidgetResizable(true);

    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->addWidget(filterGridScrollArea);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    updateDisplay();
}

static QSpinBox *makeWordIndexSpin()
{
    auto result = new QSpinBox;
    result->setMinimum(-1);
    result->setMaximum(8192); // some random "big" value here
    result->setSpecialValueText(QSL("any"));
    result->setValue(-1);
    return result;
}

void DataExtractionEditor::setSubFilters(const QVector<DataFilter> &subFilters)
{
    m_subFilters = subFilters;
    updateDisplay();
}

void DataExtractionEditor::updateDisplay()
{
    {
        QLayoutItem *item;
        while ((item = m_filterGrid->takeAt(0)) != nullptr)
        {
            auto widget = item->widget();
            delete item;
            delete widget;
        }
    }

    m_filterEdits.clear();

    s32 row = 0;


    // Add column headings
    auto make_heading_label = [](const QString &text)
    {
        auto result = new QLabel(text);
        auto font = result->font();
        font.setBold(true);
        result->setFont(font);
        result->setAlignment(Qt::AlignHCenter);
        return result;
    };

    m_filterGrid->addWidget(make_heading_label(QSL("Filter #")),       row, 0);
    m_filterGrid->addWidget(make_heading_label(QSL("Filter String")),  row, 1);
    m_filterGrid->addWidget(make_heading_label(QSL("WordIndex")),      row, 2);

    ++row;

    s32 subFilterCount = m_subFilters.size();
    for (s32 filterIndex = 0; filterIndex < subFilterCount; ++filterIndex)
    {
        auto filter = m_subFilters[filterIndex];

        auto filterLabel = new QLabel(QString("Filter %1").arg(filterIndex));

        auto filterEdit = makeFilterEdit();
        filterEdit->setFilter(filter);

        auto indexSpin = makeWordIndexSpin();
        indexSpin->setValue(filter.matchWordIndex);

        m_filterGrid->addWidget(filterLabel, row, 0);
        m_filterGrid->addWidget(filterEdit, row, 1);
        m_filterGrid->addWidget(indexSpin, row, 2);

        if (filterIndex == subFilterCount - 1)
        {
            auto pb_removeFilter = new QToolButton;
            pb_removeFilter->setIcon(QIcon(QSL(":/list_remove.png")));
            m_filterGrid->addWidget(pb_removeFilter, row, 3);
            connect(pb_removeFilter, &QPushButton::clicked, this, [this]() {
                apply();
                m_subFilters.pop_back();
                updateDisplay();
            });
            pb_removeFilter->setEnabled(subFilterCount > 1);

            auto pb_addFilter = new QToolButton;
            pb_addFilter->setIcon(QIcon(QSL(":/list_add.png")));
            m_filterGrid->addWidget(pb_addFilter, row, 4);
            connect(pb_addFilter, &QPushButton::clicked, this, [this]() {
                apply();
                auto newFilter = make_filter(defaultNewFilter);
                m_subFilters.push_back(newFilter);
                updateDisplay();
            });
        }

        m_filterEdits.push_back({filterEdit, indexSpin});

        ++row;
    }

    m_filterGrid->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Expanding), row, 0);
    ++row;
}

void DataExtractionEditor::apply()
{
    Q_ASSERT(m_subFilters.size() == m_filterEdits.size());

    s32 maxCount = m_subFilters.size();

    for (s32 i = 0; i < maxCount; ++i)
    {
        m_subFilters[i] = makeFilterFromString(m_filterEdits[i].le_filter->getFilterString(),
                                               m_filterEdits[i].spin_index->value());
    }
}

} // end namespace ui
} // end namespace analysis
