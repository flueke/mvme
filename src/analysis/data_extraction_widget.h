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

        void updateDisplay();
        void apply();

        QByteArray m_defaultFilter;
        QVector<DataFilter> m_subFilters;
        QString m_unitString;
#if 0
        double m_unitMin;
        double m_unitMax;
        bool m_generateRawDisplay;
        bool m_advancedView;
#endif

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
