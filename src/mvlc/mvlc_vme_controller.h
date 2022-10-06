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
#ifndef __MVME_MVLC_VME_CONTROLLER_H__
#define __MVME_MVLC_VME_CONTROLLER_H__

#include <QTimer>
#include "libmvme_mvlc_export.h"
#include "vme_controller.h"
#include "mvlc/mvlc_qt_object.h"

namespace mesytec
{
namespace mvme_mvlc
{

// Implementation of the VMEController interface for the MVLC.
// Note: MVLC_VMEController does not take ownership of the MVLCObject but
// ownership can be transferred via QObject::setParent() if desired.
class LIBMVME_MVLC_EXPORT MVLC_VMEController: public VMEController
{
    Q_OBJECT
    signals:
        void stackErrorNotification(const QVector<u32> &notification);

    public:
        MVLC_VMEController(MVLCObject *mvlc, QObject *parent = nullptr);

        //
        // VMEController implementation
        //
        bool isOpen() const override;
        VMEError open() override;
        VMEError close() override;
        ControllerState getState() const override;
        QString getIdentifyingString() const override;
        VMEControllerType getType() const override;

        VMEError write32(u32 address, u32 value, u8 amod) override;
        VMEError write16(u32 address, u16 value, u8 amod) override;

        VMEError read32(u32 address, u32 *value, u8 amod) override;
        VMEError read16(u32 address, u16 *value, u8 amod) override;

        // Note: The MVLC does not use the FIFO flag, instead it does something
        // "somewhat illegal": it doesn't interrupt the block transfer after 256
        // cycles and instead just keeps on reading. This means it's up to the
        // module on whether to increment the initial address or not. FIFO mode
        // only makes a difference if the controller interrupts the block
        // transfer and then starts a new one until the desired max cycles are
        // reached.
        VMEError blockRead(u32 address, u32 transfers,
                           QVector<u32> *dest, u8 amod, bool /*fifo*/) override;

        VMEError blockRead(u32 address, const mesytec::mvlc::Blk2eSSTRate &rate, u16 transfers, QVector<u32> *dest);

        VMEError blockReadSwapped(u32 address, u16 transfers, QVector<u32> *dest);
        VMEError blockReadSwapped(u32 address, const mesytec::mvlc::Blk2eSSTRate &rate, u16 transfers, QVector<u32> *dest);

        //
        // MVLC specific methods
        //
        MVLCObject *getMVLCObject() { return m_mvlc; }
        mvlc::MVLC getMVLC() { return m_mvlc->getMVLC(); }
        mvlc::ConnectionType connectionType() const { return m_mvlc->connectionType(); }
        mvlc::MVLCBasicInterface *getImpl() { return m_mvlc->getImpl(); }
        mvlc::Locks &getLocks() { return m_mvlc->getLocks(); }

#if 0
    public slots:
        void enableNotificationPolling() { m_notificationPoller.enablePolling(); }
        void disableNotificationPolling() { m_notificationPoller.disablePolling(); }
#endif

    private slots:
        void onMVLCStateChanged(const MVLCObject::State &oldState,
                                const MVLCObject::State &newState);

    private:
        MVLCObject *m_mvlc;
};

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_VME_CONTROLLER_H__ */
