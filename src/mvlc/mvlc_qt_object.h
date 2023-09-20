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
#ifndef __MVLC_QT_OBJECT_H__
#define __MVLC_QT_OBJECT_H__

#include <memory>
#include <mutex>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "libmvme_mvlc_export.h"
#include "sis3153.h"
#include "typedefs.h"
#include "util.h"

namespace mesytec
{
namespace mvme_mvlc
{

class LIBMVME_MVLC_EXPORT MVLCObject: public QObject
{
    Q_OBJECT
    public:
        enum State
        {
            Disconnected,
            Connecting,
            Connected,
        };

    signals:
        void stateChanged(const mesytec::mvme_mvlc::MVLCObject::State &oldState,
                          const mesytec::mvme_mvlc::MVLCObject::State &newState);

    public:
        explicit MVLCObject(mvlc::MVLC mvlc, QObject *parent = nullptr);
        virtual ~MVLCObject();

        State getState() const { return m_state; }

        QString getConnectionInfo() const
        {
            return QString::fromStdString(connectionInfo())
                + (QSL(", hwId=0x%1, fwRev=0x%2")
                   .arg(m_mvlc.hardwareId(), 4, 16, QLatin1Char('0'))
                   .arg(m_mvlc.firmwareRevision(), 4, 16, QLatin1Char('0')))
                ;
        }

        //
        // MVLCBasicInterface
        //
    public slots:
        std::error_code connect()
        {
            if (!isConnected())
            {
                setState(Connecting);
                return updateState(m_mvlc.connect());
            }

            return {};
        }

        std::error_code disconnect()
        {
            return updateState(m_mvlc.disconnect());
        }

    public:
        bool isConnected() const { return m_mvlc.isConnected(); }
        mvlc::ConnectionType connectionType() const { return m_mvlc.connectionType(); }
        std::string connectionInfo() const { return m_mvlc.connectionInfo(); }

        void setDisableTriggersOnConnect(bool b)
        {
            m_mvlc.setDisableTriggersOnConnect(b);
        }

        bool disableTriggersOnConnect() const
        {
            return m_mvlc.disableTriggersOnConnect();
        }

        // register and vme api
        std::error_code readRegister(u16 address, u32 &value)
        {
            return updateState(m_mvlc.readRegister(address, value));
        }

        std::error_code writeRegister(u16 address, u32 value)
        {
            return updateState(m_mvlc.writeRegister(address, value));
        }

        std::error_code vmeRead(
            u32 address, u32 &value, u8 amod, mvlc::VMEDataWidth dataWidth)
        {
            return updateState(m_mvlc.vmeRead(address, value, amod, dataWidth));
        }

        std::error_code vmeWrite(
            u32 address, u32 value, u8 amod, mvlc::VMEDataWidth dataWidth)
        {
            return updateState(m_mvlc.vmeWrite(address, value, amod, dataWidth));
        }

        std::error_code vmeBlockRead(
            u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
        {
            return updateState(m_mvlc.vmeBlockRead(address, amod, maxTransfers, dest, fifo));
        }

        std::error_code vmeBlockRead(
            u32 address, const mesytec::mvlc::Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
        {
            return updateState(m_mvlc.vmeBlockRead(address, rate, maxTransfers, dest, fifo));
        }

        std::error_code vmeBlockReadSwapped(
            u32 address, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
        {
            return updateState(m_mvlc.vmeBlockReadSwapped(address, maxTransfers, dest, fifo));
        }

        std::error_code vmeBlockReadSwapped(
            u32 address, const mesytec::mvlc::Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest, bool fifo = true)
        {
            return updateState(m_mvlc.vmeBlockReadSwapped(address, rate, maxTransfers, dest, fifo));
        }

        // stack uploading
        std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<mvlc::StackCommand> &commands)
        {
            return updateState(m_mvlc.uploadStack(stackOutputPipe, stackMemoryOffset, commands));
        }

        inline std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const mvlc::StackCommandBuilder &stack)
        {
            return uploadStack(stackOutputPipe, stackMemoryOffset, stack.getCommands());
        }

        std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents)
        {
            return updateState(m_mvlc.uploadStack(stackOutputPipe, stackMemoryOffset, stackContents));
        }

        //
        // Stack Error Notifications (Command Pipe)
        //

        mvlc::StackErrorCounters getStackErrorCounters() const
        {
            return m_mvlc.getStackErrorCounters();
        }

        void resetStackErrorCounters()
        {
            m_mvlc.resetStackErrorCounters();
        }

        //
        // Access to the low-level implementation and the per-pipe locks.
        //

        mvlc::MVLC getMVLC() { return m_mvlc; }
        mvlc::MVLCBasicInterface *getImpl() { return m_mvlc.getImpl(); }
        inline mvlc::Locks &getLocks() { return m_mvlc.getLocks(); }

        std::error_code superTransaction(const mvlc::SuperCommandBuilder &superBuilder, std::vector<u32> &dest)
        {
            return m_mvlc.superTransaction(superBuilder, dest);
        }

        std::error_code stackTransaction(const mvlc::StackCommandBuilder &stackBuilder, std::vector<u32> &dest)
        {
            return m_mvlc.stackTransaction(stackBuilder, dest);
        }

    private:
        void setState(const State &newState);

        std::error_code updateState(const std::error_code &ec)
        {
            if (ec == mvlc::ErrorType::ConnectionError || !m_mvlc.isConnected())
                setState(Disconnected);
            else if (m_mvlc.isConnected())
                setState(Connected);
            else
                InvalidCodePath;

            return ec;
        }

        mvlc::MVLC m_mvlc;
        State m_state;
};

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVLC_QT_OBJECT_H__ */
