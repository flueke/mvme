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
#include "mvlc/mvlc_basic_mvlc.h"
#include "mvlc/mvlc_error.h"

namespace mesytec
{
namespace mvme_mvlc
{

BasicMVLC::BasicMVLC(std::unique_ptr<AbstractImpl> impl)
    : m_impl(std::move(impl))
{
}

BasicMVLC::~BasicMVLC()
{
    disconnect();
}

bool BasicMVLC::isConnected() const
{
    return m_impl->isConnected();
}

std::error_code BasicMVLC::connect()
{
    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    return m_impl->connect();
}

std::error_code BasicMVLC::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    return m_impl->disconnect();
}

std::error_code BasicMVLC::write(Pipe pipe, const u8 *buffer, size_t size,
                                  size_t &bytesTransferred)
{
    auto ec = m_impl->write(pipe, buffer, size, bytesTransferred);
    return ec;
}

std::error_code BasicMVLC::read(Pipe pipe, u8 *buffer, size_t size,
                                 size_t &bytesTransferred)
{
    auto ec = m_impl->read(pipe, buffer, size, bytesTransferred);
    return ec;
}

std::pair<std::error_code, size_t> BasicMVLC::write(Pipe pipe, const QVector<u32> &buffer)
{
    size_t bytesTransferred = 0u;
    const size_t bytesToTransfer = buffer.size() * sizeof(u32);
    auto ec = write(pipe, reinterpret_cast<const u8 *>(buffer.data()),
                    bytesToTransfer, bytesTransferred);

    if (!ec && bytesToTransfer != bytesTransferred)
    {
        return std::make_pair(
            make_error_code(MVLCErrorCode::ShortWrite),
            bytesTransferred);
    }

    return std::make_pair(ec, bytesTransferred);
}

AbstractImpl *BasicMVLC::getImpl()
{
    return m_impl.get();
}

void BasicMVLC::setReadTimeout(Pipe pipe, unsigned ms)
{
    m_impl->setReadTimeout(pipe, ms);
}

void BasicMVLC::setWriteTimeout(Pipe pipe, unsigned ms)
{
    m_impl->setWriteTimeout(pipe, ms);
}

unsigned BasicMVLC::getReadTimeout(Pipe pipe) const
{
    return m_impl->getReadTimeout(pipe);
}

unsigned BasicMVLC::getWriteTimeout(Pipe pipe) const
{
    return m_impl->getWriteTimeout(pipe);
}

} // end namespace mvme_mvlc
} // end namespace mesytec
