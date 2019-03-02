#ifndef __MVLC_IMPL_FACTORY_H__
#define __MVLC_IMPL_FACTORY_H__

#include <memory>
#include "mvlc/mvlc_impl_abstract.h"

namespace mesytec
{
namespace mvlc
{

// TODO: support URI schemes like:
// usb://               <- first device having the description "MVLC"
// usb://serial=1234    <- MVLC with serial 1234
// usb://0              <- by absolute FTDI index, not checking for "MVLC" in the description
// udp://<host>
//std::unique_ptr<AbstractImpl> make_mvlc(const std::string &uri);

std::unique_ptr<AbstractImpl> make_mvlc_usb();
//std::unique_ptr<AbstractImpl> make_mvlc_udp(const std::string &host);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_IMPL_FACTORY_H__ */
