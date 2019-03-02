#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_impl_usb.h"

namespace mesytec
{
namespace mvlc
{

std::unique_ptr<AbstractImpl> make_mvlc_usb()
{
    return std::make_unique<usb::Impl>(0);
}

} // end namespace mvlc
} // end namespace mesytec
