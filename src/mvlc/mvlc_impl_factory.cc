#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_impl_eth.h"
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

std::unique_ptr<AbstractImpl> make_mvlc_usb_using_serial(const std::string &serial)
{
    return std::make_unique<usb::Impl>(serial);
}

//
// UDP
//
std::unique_ptr<AbstractImpl> make_mvlc_eth()
{
    return std::make_unique<eth::Impl>();
}

std::unique_ptr<AbstractImpl> make_mvlc_eth(const char *host)
{
    assert(host);
    return std::make_unique<eth::Impl>(host);
}

} // end namespace mvlc
} // end namespace mesytec
