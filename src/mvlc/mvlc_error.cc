#include "mvlc/mvlc_error.h"

namespace
{

class MVLCProtocolErrorCategory: public std::error_category
{
    const char *name() const noexcept
    {
        return "mvlc_protocol_error";
    }

    std::string message(int ev) const
    {
        using mesytec::mvlc::MVLCProtocolError;

        switch (static_cast<MVLCProtocolError>(ev))
        {
            case MVLCProtocolError::NoError:
                return "No Error";
            case MVLCProtocolError::IsOpen:
                return "Device is open";
            case MVLCProtocolError::IsClosed:
                return "Device is closed";
            case MVLCProtocolError::ShortWrite:
                return "Short write";
            case MVLCProtocolError::ShortRead:
                return "Short read";
            case MVLCProtocolError::MirrorShortRequest:
                return "mirror check - short request";
            case MVLCProtocolError::MirrorShortResponse:
                return "mirror check - short response";
            case MVLCProtocolError::MirrorResponseTooShort:
                return "mirror check - response too short";
            case MVLCProtocolError::MirrorNotEqual:
                return "mirror check - unequal data words";
            case MVLCProtocolError::ParseResponseUnexpectedSize:
                return "parsing - unexpected response size";
            case MVLCProtocolError::ParseUnexpectedBufferType:
                return "parsing - unexpected response buffer type";
            case MVLCProtocolError::NoVMEResponse:
                return "no VME response";
        }

        return "Unrecognized Error";
    }
};

const MVLCProtocolErrorCategory theMVLCProtocolErrorCategory {};

} // end anon namespace

namespace mesytec
{
namespace mvlc
{

std::error_code make_error_code(MVLCProtocolError error)
{
    return { static_cast<int>(error), theMVLCProtocolErrorCategory };
}

} // end namespace mvlc
} // end namespace mesytec
