/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#ifndef __DAQCONTROL_WIDGET_H__
#define __DAQCONTROL_WIDGET_H__

#include <QMenu>
#include <QWidget>

namespace Ui
{
    class DAQControlWidget;
}

class MVMEContext;

class DAQControlWidget: public QWidget
{
    Q_OBJECT
    public:
        DAQControlWidget(MVMEContext *context, QWidget *parent = 0);
        ~DAQControlWidget();

    private:
        void updateWidget();

        Ui::DAQControlWidget *ui;
        MVMEContext *m_context;
        QMenu *m_menuStartButton;
        QMenu *m_menuOneCycleButton;
};

#endif /* __DAQCONTROL_WIDGET_H__ */
