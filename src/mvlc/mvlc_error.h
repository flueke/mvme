#ifndef __MVLC_ERROR_H__
#define __MVLC_ERROR_H__

#include <system_error>

namespace mesytec
{
namespace mvlc
{

enum class MVLCErrorCode
{
    NoError,
    IsConnected,
    IsDisconnected,
    ShortWrite,
    ShortRead,
    MirrorEmptyRequest,  // size < 1
    MirrorEmptyResponse, // size < 1
    MirrorShortResponse,
    MirrorNotEqual,
    InvalidBufferHeader,
    UnexpectedResponseSize, // wanted N words, got M words
    NoVMEResponse,
};

std::error_code make_error_code(MVLCErrorCode error);

enum class ErrorType
{
    Success,
    ConnectionError,
    IOError,
    Timeout,
    ShortTransfer,
    ProtocolError,
    VMEError
};

std::error_condition make_error_condition(ErrorType et);

} // end namespace mvlc
} // end namespace mesytec

namespace std
{
    template<> struct is_error_code_enum<mesytec::mvlc::MVLCErrorCode>: true_type {};
    template<> struct is_error_condition_enum<mesytec::mvlc::ErrorType>: true_type {};
} // end namespace std

#endif /* __MVLC_ERROR_H__ */
