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
#include <gtest/gtest.h>
#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_impl_usb.h"
#include <iostream>

using std::cout;
using std::endl;

using namespace mesytec::mvme_mvlc;
using namespace mesytec::mvme_mvlc::usb;

TEST(TestMVLCError, MVLCErrorCode_to_ErrorType)
{
    using MEC = MVLCErrorCode;

    ASSERT_EQ(make_error_code(MEC::NoError), ErrorType::Success);

    for (auto code: { MEC::IsConnected, MEC::IsDisconnected })
        ASSERT_EQ(make_error_code(code), ErrorType::ConnectionError);

    for (auto code: { MEC::ShortWrite, MEC::ShortRead })
        ASSERT_EQ(make_error_code(code), ErrorType::ShortTransfer);

    for (auto code: { MEC::MirrorEmptyRequest, MEC::MirrorEmptyResponse,
                      MEC::MirrorShortResponse, MEC::MirrorNotEqual,
                      MEC::InvalidBufferHeader})
    {
        ASSERT_EQ(make_error_code(code), ErrorType::ProtocolError);
    }

    ASSERT_EQ(make_error_code(MVLCErrorCode::NoVMEResponse), ErrorType::VMEError);

    //auto ec = make_error_code(MVLCErrorCode::NoVMEResponse);
    //cout << ec.category().name() << "(" << ec.value() << "): " << ec.message() << endl;
}

TEST(TestMVLCError, FT_STATUS_to_ErrorType)
{
    ASSERT_EQ(make_error_code(FT_OK), ErrorType::Success);

    for (auto code: { FT_INVALID_HANDLE, FT_DEVICE_NOT_FOUND,
        FT_DEVICE_NOT_OPENED, FT_DEVICE_NOT_CONNECTED })
        ASSERT_EQ(make_error_code(code), ErrorType::ConnectionError);

    for (int code = FT_IO_ERROR; code <= FT_NO_MORE_ITEMS; code++)
    {
        ASSERT_EQ(make_error_code(static_cast<_FT_STATUS>(code)), ErrorType::ConnectionError);
    }

    ASSERT_EQ(make_error_code(FT_TIMEOUT), ErrorType::Timeout);

    for (int code = FT_OPERATION_ABORTED; code <= FT_OTHER_ERROR; code++)
    {
        if (code != FT_DEVICE_NOT_CONNECTED)
        {
            ASSERT_EQ(make_error_code(static_cast<_FT_STATUS>(code)), ErrorType::ConnectionError);
        }
    }

    //auto ec = make_error_code(FT_NO_MORE_ITEMS);
    //cout << ec.category().name() << "(" << ec.value() << "): " << ec.message() << endl;
}
