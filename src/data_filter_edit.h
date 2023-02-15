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
