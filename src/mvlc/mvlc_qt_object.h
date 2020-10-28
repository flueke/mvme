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
#ifndef __MVLC_QT_OBJECT_H__
#define __MVLC_QT_OBJECT_H__

#include <memory>
#include <mutex>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "libmvme_mvlc_export.h"
#include "typedefs.h"
#include "util.h"

namespace mesytec
{
namespace mvme_mvlc
{

class LIBMVME_MVLC_EXPORT MVLCObject: public QObject, public mvlc::MVLCBasicInterface
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
            return QString::fromStdString(connectionInfo());
        }

        //
        // MVLCBasicInterface
        //
    public slots:
        std::error_code connect() override
        {
            if (!isConnected())
                setState(Connecting);

            return updateState(m_mvlc.connect());
        }

        std::error_code disconnect() override
        {
            return updateState(m_mvlc.disconnect());
        }

    public:
        bool isConnected() const override { return m_mvlc.isConnected(); }
        mvlc::ConnectionType connectionType() const override { return m_mvlc.connectionType(); }
        std::string connectionInfo() const override { return m_mvlc.connectionInfo(); }

        std::error_code write(mvlc::Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override
        {
            return updateState(m_mvlc.write(pipe, buffer, size, bytesTransferred));
        }

        std::error_code read(mvlc::Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override
        {
            return updateState(m_mvlc.read(pipe, buffer, size, bytesTransferred));
        }

        std::error_code setWriteTimeout(mvlc::Pipe pipe, unsigned ms) override
        {
            return updateState(m_mvlc.setWriteTimeout(pipe, ms));
        }

        std::error_code setReadTimeout(mvlc::Pipe pipe, unsigned ms) override
        {
            return updateState(m_mvlc.setReadTimeout(pipe, ms));
        }

        unsigned writeTimeout(mvlc::Pipe pipe) const override
        {
            return m_mvlc.writeTimeout(pipe);
        }

        unsigned readTimeout(mvlc::Pipe pipe) const override
        {
            return m_mvlc.readTimeout(pipe);
        }

        void setDisableTriggersOnConnect(bool b) override
        {
            m_mvlc.setDisableTriggersOnConnect(b);
        }

        bool disableTriggersOnConnect() const override
        {
            return m_mvlc.disableTriggersOnConnect();
        }

        //
        // Dialog layer
        //
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
            return updateState(m_mvlc.vmeRead(
                    address, value, amod, dataWidth));
        }

        std::error_code vmeWrite(
            u32 address, u32 value, u8 amod, mvlc::VMEDataWidth dataWidth)
        {
            return updateState(m_mvlc.vmeWrite(
                    address, value, amod, dataWidth));
        }

        std::error_code vmeBlockRead(
            u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest)
        {
            return updateState(m_mvlc.vmeBlockRead(
                    address, amod, maxTransfers, dest));
        }

        std::error_code vmeMBLTSwapped(
            u32 address, u16 maxTransfers, std::vector<u32> &dest)
        {
            return updateState(m_mvlc.vmeMBLTSwapped(
                    address, maxTransfers, dest));
        }

        std::error_code readResponse(mvlc::BufferHeaderValidator bhv, std::vector<u32> &dest)
        {
            return updateState(m_mvlc.readResponse(
                    bhv, dest));
        }

        std::error_code mirrorTransaction(
            const std::vector<u32> &cmdBuffer, std::vector<u32> &responseDest)
        {
            return updateState(m_mvlc.mirrorTransaction(
                    cmdBuffer, responseDest));
        }

        std::error_code stackTransaction(const std::vector<u32> &stackUploadData,
                                         std::vector<u32> &responseDest)
        {
            return updateState(m_mvlc.stackTransaction(
                    stackUploadData, responseDest));
        }

        std::error_code readKnownBuffer(std::vector<u32> &dest)
        {
            return updateState(m_mvlc.readKnownBuffer(dest));
        }

        std::vector<u32> getResponseBuffer() const
        {
            return m_mvlc.getResponseBuffer();
        }

        //
        // Stack Error Notifications (Command Pipe)
        //

        mvlc::StackErrorCounters getStackErrorCounters() const
        {
            return m_mvlc.getStackErrorCounters();
        }

        mvlc::Protected<mvlc::StackErrorCounters> &getProtectedStackErrorCounters()
        {
            return m_mvlc.getProtectedStackErrorCounters();
        }

        void clearStackErrorCounters()
        {
            m_mvlc.clearStackErrorCounters();
        }

        //
        // Access to the low-level implementation and the per-pipe locks.
        //

        mvlc::MVLC getMVLC() { return m_mvlc; }
        mvlc::MVLCBasicInterface *getImpl() { return m_mvlc.getImpl(); }
        inline mvlc::Locks &getLocks() { return m_mvlc.getLocks(); }

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
