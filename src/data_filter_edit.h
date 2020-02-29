#ifndef __DATA_FILTER_EDIT_H__
#define __DATA_FILTER_EDIT_H__

#include <QLineEdit>
#include "data_filter.h"

class LIBMVME_EXPORT DataFilterEdit: public QLineEdit
{
    Q_OBJECT
    public:
        explicit DataFilterEdit(QWidget *parent = nullptr);
        explicit DataFilterEdit(int bits, QWidget *parent = nullptr);
        DataFilterEdit(const QString &filterString, QWidget *parent = nullptr);
        DataFilterEdit(const DataFilter &filter, QWidget *parent = nullptr);

        // set/query number of bits. setting the bits will change the input
        // mask and truncate the current text on the left if needed.
        void setBitCount(int bits);
        int getBitCount() const { return m_bits; }

        void setFilterString(const QString &filter);
        QString getFilterString() const;

        void setFilter(const DataFilter &filter);
        DataFilter getFilter() const;

    private:
        void updateMaskAndWidth();

        int m_bits = 32;
};

// Note: obsolete wrapper from when a plain QLineEdit was used.
// Create a QLineEdit setup for convenient filter editing and display.
LIBMVME_EXPORT DataFilterEdit *makeFilterEdit(QWidget *parent = nullptr);


#endif /* __DATA_FILTER_EDIT_H__ */
