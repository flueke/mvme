#ifndef __DATA_FILTER_EDIT_H__
#define __DATA_FILTER_EDIT_H__

#include <QLineEdit>
#include "data_filter.h"

class DataFilterEdit: public QLineEdit
{
        // set/query number of bits. setting the bits will change the input
        // mask and truncate the current text on the left if needed.
        //
        // if new_bits > old_bits the previous full text could be restored (always keep the text with the max number of bits)
        // or alternatively the low bits should be kept and the new bits filled with 'X'
        // or input for each number of bits could be kept and set appropriately
    Q_OBJECT
    public:
        explicit DataFilterEdit(QWidget *parent = nullptr);
        explicit DataFilterEdit(int bits, QWidget *parent = nullptr);
        DataFilterEdit(const QString &filterString, QWidget *parent = nullptr);
        DataFilterEdit(const DataFilter &filter, QWidget *parent = nullptr);

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
LIBMVME_EXPORT DataFilterEdit *makeFilterEdit(u8 bits = 32, QWidget *parent = nullptr);


#endif /* __DATA_FILTER_EDIT_H__ */
