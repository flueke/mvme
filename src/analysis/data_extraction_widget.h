#ifndef __DATA_EXTRACTION_WIDGET_H__
#define __DATA_EXTRACTION_WIDGET_H__

#include "data_filter.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>

namespace analysis
{

class DataExtractionEditor: public QWidget
{
    Q_OBJECT
    public:
        DataExtractionEditor(QWidget *parent = 0);
        DataExtractionEditor(const QVector<DataFilter> &subFilters, QWidget *parent = 0);

        void setSubFilters(const QVector<DataFilter> &subFilters);
        void updateDisplay();
        void apply();

        QVector<DataFilter> m_subFilters;
        QGridLayout *m_filterGrid;

        struct FilterEditElements
        {
            QLineEdit *le_filter;
            QSpinBox *spin_index;
        };

        QVector<FilterEditElements> m_filterEdits;
};

} // end namespace analysis

#endif /* __DATA_EXTRACTION_WIDGET_H__ */
