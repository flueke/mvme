#ifndef __MVLC_IMPL_FACTORY_H__
#define __MVLC_IMPL_FACTORY_H__

#include <memory>
#include "mvlc/mvlc_impl_abstract.h"
#include "mvlc/mvlc_constants.h"
#include "libmvme_mvlc_export.h"

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

std::unique_ptr<AbstractImpl> LIBMVME_MVLC_EXPORT make_mvlc_usb();
std::unique_ptr<AbstractImpl> LIBMVME_MVLC_EXPORT make_mvlc_usb_using_index(int index);
std::unique_ptr<AbstractImpl> LIBMVME_MVLC_EXPORT make_mvlc_usb_using_serial(unsigned serial);
std::unique_ptr<AbstractImpl> LIBMVME_MVLC_EXPORT make_mvlc_usb_using_serial(const std::string &serial);

std::unique_ptr<AbstractImpl> LIBMVME_MVLC_EXPORT make_mvlc_udp();
std::unique_ptr<AbstractImpl> LIBMVME_MVLC_EXPORT make_mvlc_udp(const char *host);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_IMPL_FACTORY_H__ */
