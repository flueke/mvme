#include "data_extraction_widget.h"
#include "../qt_util.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace analysis
{

static const char *defaultNewFilter = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

DataExtractionEditor::DataExtractionEditor(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QSL("Data Extraction"));

    auto filterGridWidget = new QWidget;
    m_filterGrid = new QGridLayout(filterGridWidget);
    m_filterGrid->setContentsMargins(3, 3, 3, 3);
    m_filterGrid->setSpacing(6);
    m_filterGrid->setColumnStretch(1, 1);

    auto filterGridScrollArea = new QScrollArea;
    filterGridScrollArea->setWidget(filterGridWidget);
    filterGridScrollArea->setWidgetResizable(true);

    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->addWidget(filterGridScrollArea);

    updateDisplay();
}

static QLineEdit *makeFilterEdit()
{
    QFont font;
    font.setFamily(QSL("Monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(9);

    QLineEdit *result = new QLineEdit;
    result->setFont(font);
    result->setInputMask("NNNN NNNN NNNN NNNN NNNN NNNN NNNN NNNN");

    QFontMetrics fm(font);
    s32 padding = 6;
    s32 width = fm.width(result->inputMask()) + padding;
    result->setMinimumWidth(width);

    return result;
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

    // default filter
    QLineEdit *defaultFilterEdit = makeFilterEdit();
    defaultFilterEdit->setReadOnly(true);
    defaultFilterEdit->setFocusPolicy(Qt::NoFocus);
    QPalette palette;
    palette.setColor(QPalette::Base, QColor(QSL("#e6e4e1")));
    defaultFilterEdit->setPalette(palette);

    defaultFilterEdit->setText(m_defaultFilter);

    m_filterGrid->addWidget(new QLabel(QSL("Default Filter")), row, 0);
    m_filterGrid->addWidget(defaultFilterEdit, row, 1);
    ++row;

    s32 subFilterCount = m_subFilters.size();
    for (s32 filterIndex = 0; filterIndex < subFilterCount; ++filterIndex)
    {
        auto filter = m_subFilters[filterIndex];

        auto filterLabel = new QLabel(QString("Filter %1").arg(filterIndex));

        auto filterEdit = makeFilterEdit();
        filterEdit->setText(filter.getFilter());

        auto indexSpin = makeWordIndexSpin();
        indexSpin->setValue(filter.getWordIndex());

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
                DataFilter newFilter(defaultNewFilter);
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
        m_subFilters[i] = makeFilterFromString(m_filterEdits[i].le_filter->text(),
                                               m_filterEdits[i].spin_index->value());
    }
}

} // end namespace analysis
