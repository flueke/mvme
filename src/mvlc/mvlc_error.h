#ifndef __MVLC_ERROR_H__
#define __MVLC_ERROR_H__

#include <system_error>

namespace mesytec
{
namespace mvlc
{

enum class MVLCProtocolError
{
    NoError,
    IsOpen,
    IsClosed,
    ShortWrite,
    ShortRead,
    MirrorShortRequest,  // size < 1
    MirrorShortResponse, // size < 1
    MirrorResponseTooShort,
    MirrorNotEqual,
    ParseResponseUnexpectedSize,
    ParseUnexpectedBufferType,
    NoVMEResponse,
};

std::error_code make_error_code(MVLCProtocolError error);

} // end namespace mvlc
} // end namespace mesytec

namespace std
{
    template<> struct is_error_code_enum<mesytec::mvlc::MVLCProtocolError>: true_type {};
} // end namespace std

#endif /* __MVLC_ERROR_H__ */
