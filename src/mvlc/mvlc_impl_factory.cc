#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_impl_udp.h"
#include "mvlc/mvlc_impl_usb.h"
#include <cassert>

namespace mesytec
{
namespace mvlc
{

//
// USB
//
std::unique_ptr<AbstractImpl> make_mvlc_usb()
{
    return std::make_unique<usb::Impl>();
}

std::unique_ptr<AbstractImpl> make_mvlc_usb(unsigned index)
{
    return std::make_unique<usb::Impl>(index);
}

std::unique_ptr<AbstractImpl> make_mvlc_usb_using_serial(unsigned serial)
{
    return make_mvlc_usb_using_serial(usb::format_serial(serial));
}

std::unique_ptr<AbstractImpl> make_mvlc_usb_using_serial(const std::string &serial)
{
    return std::make_unique<usb::Impl>(serial);
}

//
// UDP
//
std::unique_ptr<AbstractImpl> make_mvlc_udp()
{
    return std::make_unique<udp::Impl>();
}

std::unique_ptr<AbstractImpl> make_mvlc_udp(const char *host)
{
    assert(host);
    return std::make_unique<udp::Impl>(host);
}

} // end namespace mvlc
} // end namespace mesytec
