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

            case MVLCProtocolError::MirrorEmptyRequest:
                return "mirror check: empty request";

            case MVLCProtocolError::MirrorEmptyResponse:
                return "mirror check: empty response";

            case MVLCProtocolError::MirrorShortResponse:
                return "mirror check: response too short";

            case MVLCProtocolError::MirrorNotEqual:
                return "mirror check: unequal data words";

            case MVLCProtocolError::InvalidBufferHeader:
                return "invalid buffer header";

            case MVLCProtocolError::UnexpectedResponseSize:
                return "unexpected response size";

            case MVLCProtocolError::NoVMEResponse:
                return "no VME response";
        }

        return "unrecognized MVLC error";
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
