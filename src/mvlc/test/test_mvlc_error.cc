#include <gtest/gtest.h>
#include "mvlc/mvlc_error.h"
#include "mvlc/mvlc_impl_usb.h"
#include <iostream>

using std::cout;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::usb;

TEST(TestMVLCError, MVLCErrorCode_to_ErrorType)
{
    using MEC = MVLCErrorCode;

    ASSERT_EQ(make_error_code(MEC::NoError), ErrorType::Success);

    for (auto code: { MEC::IsConnected, MEC::IsDisconnected })
        ASSERT_EQ(make_error_code(code), ErrorType::ConnectionError);

    for (auto code: { MEC::ShortWrite, MEC::ShortRead })
        ASSERT_EQ(make_error_code(MVLCErrorCode::ShortWrite), ErrorType::ShortTransfer);

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
        ASSERT_EQ(make_error_code(static_cast<_FT_STATUS>(code)), ErrorType::IOError);
    }

    ASSERT_EQ(make_error_code(FT_TIMEOUT), ErrorType::Timeout);

    for (int code = FT_OPERATION_ABORTED; code <= FT_OTHER_ERROR; code++)
    {
        if (code != FT_DEVICE_NOT_CONNECTED)
        {
            ASSERT_EQ(make_error_code(static_cast<_FT_STATUS>(code)), ErrorType::IOError);
        }
    }

    //auto ec = make_error_code(FT_NO_MORE_ITEMS);
    //cout << ec.category().name() << "(" << ec.value() << "): " << ec.message() << endl;
}
