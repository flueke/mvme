#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_error.h"

namespace mesytec
{
namespace mvlc
{

//
// MVLCObject
//
MVLCObject::MVLCObject(std::unique_ptr<AbstractImpl> impl, QObject *parent)
    : QObject(parent)
    , m_impl(std::move(impl))
    , m_state(Disconnected)
{
    if (m_impl->is_connected())
        setState(Connected);
}

MVLCObject::~MVLCObject()
{
    disconnect();
}

bool MVLCObject::isConnected() const
{
    return m_impl->is_connected();
}

std::error_code MVLCObject::connect()
{
    if (isConnected()) return make_error_code(MVLCProtocolError::IsOpen);

    std::unique_lock<std::mutex> cmdLock(m_cmdMutex, std::defer_lock);
    std::unique_lock<std::mutex> dataLock(m_dataMutex, std::defer_lock);
    std::lock(cmdLock, dataLock);

    setState(Connecting);
    auto ec = m_impl->connect();

    if (ec)
    {
        setState(Disconnected);
    }
    else
    {
        setState(Connected);
    }

    return ec;
}

std::error_code MVLCObject::disconnect()
{
    if (!isConnected()) return make_error_code(MVLCProtocolError::IsClosed);

    std::unique_lock<std::mutex> cmdLock(m_cmdMutex, std::defer_lock);
    std::unique_lock<std::mutex> dataLock(m_dataMutex, std::defer_lock);
    std::lock(cmdLock, dataLock);

    auto ec = m_impl->disconnect();
    setState(Disconnected);
    return ec;
}

void MVLCObject::setState(const State &newState)
{
    if (m_state != newState)
    {
        auto prevState = m_state;
        m_state = newState;
        emit stateChanged(prevState, newState);
    }
};

std::error_code MVLCObject::write(Pipe pipe, const u8 *buffer, size_t size,
                                  size_t &bytesTransferred)
{
    auto ec = m_impl->write(pipe, buffer, size, bytesTransferred);
    return ec;
}

std::error_code MVLCObject::read(Pipe pipe, u8 *buffer, size_t size,
                                 size_t &bytesTransferred)
{
    auto ec = m_impl->read(pipe, buffer, size, bytesTransferred);
    return ec;
}

std::pair<std::error_code, size_t> MVLCObject::write(Pipe pipe, const QVector<u32> &buffer)
{
    size_t bytesTransferred = 0u;
    const size_t bytesToTransfer = buffer.size() * sizeof(u32);
    auto ec = write(pipe, reinterpret_cast<const u8 *>(buffer.data()),
                    bytesToTransfer, bytesTransferred);

    if (!ec && bytesToTransfer != bytesTransferred)
    {
        return std::make_pair(
            make_error_code(MVLCProtocolError::ShortWrite),
            bytesTransferred);
    }

    return std::make_pair(ec, bytesTransferred);
}

AbstractImpl *MVLCObject::getImpl()
{
    return m_impl.get();
}

void MVLCObject::setReadTimeout(Pipe pipe, unsigned ms)
{
    m_impl->set_read_timeout(pipe, ms);
}

void MVLCObject::setWriteTimeout(Pipe pipe, unsigned ms)
{
    m_impl->set_write_timeout(pipe, ms);
}

unsigned MVLCObject::getReadTimeout(Pipe pipe) const
{
    return m_impl->get_read_timeout(pipe);
}

unsigned MVLCObject::getWriteTimeout(Pipe pipe) const
{
    return m_impl->get_write_timeout(pipe);
}


} // end namespace mvlc
} // end namespace mesytec
