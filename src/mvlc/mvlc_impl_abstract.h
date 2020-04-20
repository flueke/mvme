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
#ifndef __MVLC_ABSTRACT_IMPL_H__
#define __MVLC_ABSTRACT_IMPL_H__

#include <system_error>
#include "mvlc/mvlc_constants.h"
#include "typedefs.h"

namespace mesytec
{
namespace mvme_mvlc
{

class AbstractImpl
{
    public:
        virtual std::error_code connect() = 0;
        virtual std::error_code disconnect() = 0;
        virtual bool isConnected() const = 0;

        virtual std::error_code setWriteTimeout(Pipe pipe, unsigned ms) = 0;
        virtual std::error_code setReadTimeout(Pipe pipe, unsigned ms) = 0;

        virtual unsigned getWriteTimeout(Pipe pipe) const = 0;
        virtual unsigned getReadTimeout(Pipe pipe) const = 0;

        virtual std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                                      size_t &bytesTransferred) = 0;

        virtual std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                                     size_t &bytesTransferred) = 0;

        virtual std::error_code getReadQueueSize(Pipe pipe, u32 &dest) = 0;

        virtual ConnectionType connectionType() const = 0;
        virtual std::string connectionInfo() const = 0;

        AbstractImpl() = default;
        AbstractImpl &operator=(const AbstractImpl &) = delete;
        AbstractImpl(const AbstractImpl &) = delete;
        virtual ~AbstractImpl() {};
};

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVLC_ABSTRACT_IMPL_H__ */
