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

            case MVLCErrorCode::NoResponseReceived:
                return "no command response received from MVLC";

            case MVLCErrorCode::UnexpectedResponseSize:
                return "unexpected response size";

            case MVLCErrorCode::CommandArgOutOfRange:
                return "command argument out of range";

            case MVLCErrorCode::NoVMEResponse:
                return "no VME response";

            case MVLCErrorCode::HostLookupError:
                return "host lookup failed";

            case MVLCErrorCode::EmptyHostname:
                return "empty hostname/ip given";

            case MVLCErrorCode::BindLocalError:
                 return "could not bind local sockets";

            case MVLCErrorCode::InvalidPipe:
                 return "invalid pipe/endpoint";

            case MVLCErrorCode::SocketError:
                 return "generic socket error";

            case MVLCErrorCode::SocketReadTimeout:
                 return "socket read timeout";

            case MVLCErrorCode::SocketWriteTimeout:
                 return "socket write timeout";

            case MVLCErrorCode::UDPPacketChannelOutOfRange:
                 return "UDP packet channel out of range";

            case MVLCErrorCode::StackCountExceeded:
                 return "number of stacks exceeded";

            case MVLCErrorCode::StackMemoryExceeded:
                 return "MVLC stack memory exceeded";

            case MVLCErrorCode::StackSyntaxError:
                 return "Stack syntax error";

            case MVLCErrorCode::InvalidStackHeader:
                 return "Invalid stack header";

            case MVLCErrorCode::TimerCountExceeded:
                 return "Timer count exceeded";

            case MVLCErrorCode::ReadoutSetupError:
                 return "Generic Readout Setup Error";

            case MVLCErrorCode::StackIndexOutOfRange:
                 return "Stack index out of range";

            case MVLCErrorCode::UnexpectedBufferHeader:
                 return "Unexpected buffer header";

            case MVLCErrorCode::NeedMoreData:
                 return "Need more data";

            case MVLCErrorCode::InUse:
                 return "MVLC is in use";

            case MVLCErrorCode::USBChipConfigError:
                 return "Incorrect USB chip configuration (FTDI)";
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
            case MVLCErrorCode::HostLookupError:
            case MVLCErrorCode::BindLocalError:
            case MVLCErrorCode::SocketError:
            case MVLCErrorCode::EmptyHostname:
            case MVLCErrorCode::InUse:
            case MVLCErrorCode::USBChipConfigError:
                return ErrorType::ConnectionError;

            case MVLCErrorCode::ShortWrite:
            case MVLCErrorCode::ShortRead:
                return ErrorType::ShortTransfer;

            case MVLCErrorCode::MirrorEmptyRequest:
            case MVLCErrorCode::MirrorEmptyResponse:
            case MVLCErrorCode::MirrorShortResponse:
            case MVLCErrorCode::MirrorNotEqual:
            case MVLCErrorCode::InvalidBufferHeader:
            case MVLCErrorCode::NoResponseReceived:
            case MVLCErrorCode::UnexpectedResponseSize:
            case MVLCErrorCode::CommandArgOutOfRange:
            case MVLCErrorCode::InvalidPipe:
            case MVLCErrorCode::StackCountExceeded:
            case MVLCErrorCode::StackMemoryExceeded:
            case MVLCErrorCode::StackSyntaxError:
            case MVLCErrorCode::InvalidStackHeader:
            case MVLCErrorCode::TimerCountExceeded: // FIXME: does not belong here
            case MVLCErrorCode::ReadoutSetupError:  // FIXME: does not belong here
            case MVLCErrorCode::StackIndexOutOfRange:
            case MVLCErrorCode::UnexpectedBufferHeader:
            case MVLCErrorCode::NeedMoreData:
            case MVLCErrorCode::UDPPacketChannelOutOfRange:
                return ErrorType::ProtocolError;

            case MVLCErrorCode::NoVMEResponse:
                return ErrorType::VMEError;

            case MVLCErrorCode::SocketReadTimeout:
            case MVLCErrorCode::SocketWriteTimeout:
                return ErrorType::Timeout;
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

    // Equivalence between local conditions and any error code
    bool equivalent(const std::error_code &ec, int condition) const noexcept
    {
        using mesytec::mvlc::ErrorType;

        switch (static_cast<ErrorType>(condition))
        {
            case ErrorType::Timeout:
                return ec == std::error_code(EAGAIN, std::system_category());

            default:
                break;
        }

        return false;
    }
};

const ErrorTypeCategory theErrorTypeCategory;

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
    return { static_cast<int>(et), theErrorTypeCategory };
}

} // end namespace mvlc
} // end namespace mesytec
