#include "mvlc/mvlc_qt_object.h"

namespace mesytec
{
namespace mvlc
{

//
// MVLCObject
//
MVLCObject::MVLCObject(QObject *parent)
    : QObject(parent)
{
}

MVLCObject::MVLCObject(const USB_Impl &impl, QObject *parent)
    : QObject(parent)
    , m_impl(impl)
{
    if (is_open(m_impl))
        setState(Connected);
}

MVLCObject::~MVLCObject()
{
    //TODO:
    //Lock the open/close lock so that no one can inc the refcount while we're doing things.
    //if (refcount of impl would go down to zero)
    //    close_impl();

}

void MVLCObject::connect()
{
    if (isConnected()) return;

    setState(Connecting);

    usb::err_t error;
    m_impl = usb::open_by_index(0, &error);
    m_impl.readTimeout_ms = 500;

    if (!is_open(m_impl))
    {
        emit errorSignal("Error connecting to MVLC", usb::make_usb_error(error));
        setState(Disconnected);
    }
    else
    {
        setState(Connected);
    }
}

void MVLCObject::disconnect()
{
    if (!isConnected()) return;
    close(m_impl);
    setState(Disconnected);
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

} // end namespace mvlc
} // end namespace mesytec
