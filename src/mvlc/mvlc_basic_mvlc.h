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
#ifndef __MVLC_BASIC_MVLC_H__
#define __MVLC_BASIC_MVLC_H__

#include <memory>
#include <QVector>
#include "mvlc/mvlc_impl_abstract.h"
#include "libmvme_mvlc_export.h"

namespace mesytec
{
namespace mvlc
{

class LIBMVME_MVLC_EXPORT BasicMVLC
{
    public:
        BasicMVLC(std::unique_ptr<AbstractImpl> impl);
        virtual ~BasicMVLC();

        std::error_code connect();
        std::error_code disconnect();
        bool isConnected() const;
        ConnectionType connectionType() const { return m_impl->connectionType(); }

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred);

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred);

        std::pair<std::error_code, size_t> write(Pipe pipe, const QVector<u32> &buffer);

        AbstractImpl *getImpl();

        void setReadTimeout(Pipe pipe, unsigned ms);
        void setWriteTimeout(Pipe pipe, unsigned ms);

        unsigned getReadTimeout(Pipe pipe) const;
        unsigned getWriteTimeout(Pipe pipe) const;

    private:
        std::unique_ptr<AbstractImpl> m_impl;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_BASIC_MVLC_H__ */
