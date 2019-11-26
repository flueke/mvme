#include "mvlc/mvlc_basic_mvlc.h"
#include "mvlc/mvlc_error.h"

namespace mesytec
{
namespace mvlc
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

} // end namespace mvlc
} // end namespace mesytec
