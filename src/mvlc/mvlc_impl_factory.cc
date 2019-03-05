#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_impl_udp.h"
#include "mvlc/mvlc_impl_usb.h"

namespace mesytec
{
namespace mvlc
{

std::unique_ptr<AbstractImpl> make_mvlc_usb()
{
    return std::make_unique<usb::Impl>(0);
}

std::unique_ptr<AbstractImpl> make_mvlc_udp(const char *host)
{
    return std::make_unique<udp::Impl>(host);
}

} // end namespace mvlc
} // end namespace mesytec
