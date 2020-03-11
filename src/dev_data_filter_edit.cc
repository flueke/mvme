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
#include <QApplication>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include "data_filter_edit.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    DataFilterEdit *filterEdit = new DataFilterEdit;

    QPushButton *pb_makeFilter = new QPushButton("Create Filter"),
                *pb_makeFromRaw = new QPushButton("Make from raw"),
                *pb_makePatternFilter = new QPushButton("Set Pattern");

    QSpinBox *spin_bitCount = new QSpinBox;
    spin_bitCount->setMinimum(1);
    spin_bitCount->setMaximum(32);
    spin_bitCount->setValue(32);

    QLabel *label_bitCount = new QLabel;
    QLineEdit *le_rawFilter = new QLineEdit;

    QWidget container;
    auto containerLayout = new QFormLayout(&container);

    {
        auto &l = containerLayout;
        l->addRow("FilterEdit", filterEdit);
        l->addRow(pb_makeFilter);
        l->addRow(pb_makePatternFilter);
        l->addRow("Set BitCount", spin_bitCount);
        l->addRow("Get BitCount", label_bitCount);
        l->addRow("Raw Input", le_rawFilter);
        l->addRow(pb_makeFromRaw);
    }

    QObject::connect(pb_makeFilter, &QPushButton::clicked,
                     &container, [=] () {
        auto filter = filterEdit->getFilter();
        qDebug() << "got a filter: " << filter.toString();
    });

    QObject::connect(pb_makePatternFilter, &QPushButton::clicked,
                     &container, [=] () {
        filterEdit->setFilterString("1111 2222 3333 4444 5555 6666 7777 8888");
    });

    QObject::connect(spin_bitCount, static_cast<void (QSpinBox::*) (int)>(&QSpinBox::valueChanged),
                     &container, [=] (int bits) {

        filterEdit->setBitCount(bits);
        label_bitCount->setText(QString::number(filterEdit->getBitCount()));
    });

    QObject::connect(pb_makeFromRaw, &QPushButton::clicked,
                     &container, [=] () {
        DataFilter filter(le_rawFilter->text().right(32).toLocal8Bit());
        filterEdit->setFilter(filter);
    });

    container.show();

    return app.exec();
}
