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
#ifndef VME_DEBUG_WIDGET_H
#define VME_DEBUG_WIDGET_H

#include <QWidget>
#include "util.h"

namespace Ui {
class VMEDebugWidget;
}

class MVMEContext;

class VMEDebugWidget : public QWidget
{
    Q_OBJECT

public:
    VMEDebugWidget(MVMEContext *context, QWidget *parent = 0);
    ~VMEDebugWidget();

private slots:

    void on_writeLoop1_toggled(bool checked);
    void on_writeLoop2_toggled(bool checked);
    void on_writeLoop3_toggled(bool checked);

    void on_writeWrite1_clicked();
    void on_writeWrite2_clicked();
    void on_writeWrite3_clicked();

    void on_readLoop1_toggled(bool checked);
    void on_readLoop2_toggled(bool checked);
    void on_readLoop3_toggled(bool checked);

    void on_readRead1_clicked();
    void on_readRead2_clicked();
    void on_readRead3_clicked();

    void on_runScript_clicked();
    void on_saveScript_clicked();
    void on_loadScript_clicked();

private:
    void doWrite(u32 address, u32 value);
    u16 doRead(u32 address);


    Ui::VMEDebugWidget *ui;
    MVMEContext *m_context;
};

#endif // VME_DEBUG_WIDGET_H
