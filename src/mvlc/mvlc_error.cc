#include "mvlc/mvlc_error.h"
#include <cassert>

namespace
{

class MVLCErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "mvlc_error";
    }

    std::string message(int ev) const override
    {
        using mesytec::mvlc::MVLCErrorCode;

        switch (static_cast<MVLCErrorCode>(ev))
        {
            case MVLCErrorCode::NoError:
                return "No Error";

            case MVLCErrorCode::IsConnected:
                return "Device is connected";

            case MVLCErrorCode::IsDisconnected:
                return "Device is disconnected";

            case MVLCErrorCode::ShortWrite:
                return "Short write";

            case MVLCErrorCode::ShortRead:
                return "Short read";

            case MVLCErrorCode::MirrorEmptyRequest:
                return "mirror check: empty request";

            case MVLCErrorCode::MirrorEmptyResponse:
                return "mirror check: empty response";

            case MVLCErrorCode::MirrorShortResponse:
                return "mirror check: response too short";

            case MVLCErrorCode::MirrorNotEqual:
                return "mirror check: unequal data words";

            case MVLCErrorCode::InvalidBufferHeader:
                return "invalid buffer header";

            case MVLCErrorCode::UnexpectedResponseSize:
                return "unexpected response size";

            case MVLCErrorCode::NoVMEResponse:
                return "no VME response";
        }

        return "unrecognized MVLC error";
    }

    std::error_condition default_error_condition(int ev) const noexcept override
    {
        using mesytec::mvlc::MVLCErrorCode;
        using mesytec::mvlc::ErrorType;

        switch (static_cast<MVLCErrorCode>(ev))
        {
            case MVLCErrorCode::NoError:
                return ErrorType::Success;

            case MVLCErrorCode::IsConnected:
            case MVLCErrorCode::IsDisconnected:
                return ErrorType::ConnectionError;

            case MVLCErrorCode::ShortWrite:
            case MVLCErrorCode::ShortRead:
                return ErrorType::ShortTransfer;

            case MVLCErrorCode::MirrorEmptyRequest:
            case MVLCErrorCode::MirrorEmptyResponse:
            case MVLCErrorCode::MirrorShortResponse:
            case MVLCErrorCode::MirrorNotEqual:
            case MVLCErrorCode::InvalidBufferHeader:
            case MVLCErrorCode::UnexpectedResponseSize:
                return ErrorType::ProtocolError;

            case MVLCErrorCode::NoVMEResponse:
                return ErrorType::VMEError;
        }
        assert(false);
        return {};
    }
};

const MVLCErrorCategory theMVLCErrorCategory {};

class ErrorTypeCategory: public std::error_category
{
    const char *name() const noexcept
    {
        return "mvlc error type";
    }

    std::string message(int ev) const
    {
        using mesytec::mvlc::ErrorType;

        switch (static_cast<ErrorType>(ev))
        {
            case ErrorType::Success:
                return "Success";

            case ErrorType::ConnectionError:
                return "Connection Error";

            case ErrorType::IOError:
                return "I/O Error";

            case ErrorType::Timeout:
                return "Timeout";

            case ErrorType::ShortTransfer:
                return "Short Transfer";

            case ErrorType::ProtocolError:
                return "MVLC Protocol Error";

            case ErrorType::VMEError:
                return "VME Error";
        }

        return "unrecognized error type";
    }
};

const ErrorTypeCategory theErrorConditionCategory;

} // end anon namespace

namespace mesytec
{
namespace mvlc
{

std::error_code make_error_code(MVLCErrorCode error)
{
    return { static_cast<int>(error), theMVLCErrorCategory };
}

std::error_condition make_error_condition(ErrorType et)
{
    return { static_cast<int>(et), theErrorConditionCategory };
}

} // end namespace mvlc
} // end namespace mesytec
