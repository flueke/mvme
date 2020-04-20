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
#include "mvlc/mvlc_impl_factory.h"
#include "mvlc/mvlc_impl_eth.h"
#include "mvlc/mvlc_impl_usb.h"
#include <cassert>

namespace mesytec
{
namespace mvme_mvlc
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

} // end namespace mvme_mvlc
} // end namespace mesytec
