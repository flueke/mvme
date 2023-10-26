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
#ifndef __MVME_MVLC_VME_DEBUG_WIDGET_H__
#define __MVME_MVLC_VME_DEBUG_WIDGET_H__

#include "libmvme_mvlc_export.h"

#include <QWidget>
#include <memory>
#include "mvlc/mvlc_qt_object.h"

namespace Ui
{
    class VMEDebugWidget;
}

namespace mesytec
{
namespace mvme_mvlc
{

class LIBMVME_EXPORT VMEDebugWidget: public QWidget
{
    Q_OBJECT
    signals:
        void sigLogMessage(const QString &msg);

    public:
        VMEDebugWidget(MVLCObject *mvlc, QWidget *parent = 0);
        virtual ~VMEDebugWidget();

    private slots:
        void slt_writeLoop_toggled(int writerIndex, bool checked);
        void slt_doWrite_clicked(int writerIndex);

        void slt_readLoop_toggled(int readerIndex, bool checked);
        void slt_doRead_clicked(int readerIndex);

        void slt_saveScript();
        void slt_loadScript();
        void slt_runScript();

    private:
        void doWrite(u32 address, u16 value);
        u16 doSingleRead(u32 address);

        struct Private;
        std::unique_ptr<Private> d;
        std::unique_ptr<Ui::VMEDebugWidget> ui;
};

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_VME_DEBUG_WIDGET_H__ */
