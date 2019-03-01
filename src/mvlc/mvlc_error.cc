#include "mvlc/mvlc_error.h"

namespace
{

class MVLCErrorCategory: public std::error_category
{
    const char *name() const noexcept
    {
        return "mvlc_protocol_error";
    }

    std::string message(int ev) const
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
};

const MVLCErrorCategory theMVLCErrorCategory {};

} // end anon namespace

namespace mesytec
{
namespace mvlc
{

std::error_code make_error_code(MVLCErrorCode error)
{
    return { static_cast<int>(error), theMVLCErrorCategory };
}

} // end namespace mvlc
} // end namespace mesytec
