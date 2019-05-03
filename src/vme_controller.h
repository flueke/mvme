/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef VMECONTROLLER_H
#define VMECONTROLLER_H

#include "libmvme_core_export.h"
#include "vme.h"
#include "globals.h"
#include <QObject>

enum class VMEControllerType
{
    VMUSB,
    SIS3153,
    MVLC_USB,
    MVLC_ETH,
};

/* VME Controller errors and results
 * ---------------------------------
 * - Types of operations:
 *   Controller level: open, close, low-level read write (usb, ...), timeout
 *      Result: success or error code + error message (controller specific)
 *   VME level: write32, bltRead, ...
 *      Result: vme data for read operations, berr (no dtack) error for write operations
 *
 */

class LIBMVME_CORE_EXPORT VMEError
{
    public:
        enum ErrorType
        {
            NoError = 0,    // No error occured; the operation succeeded
            UnknownError,
            NotOpen,        // The controller is not open
            WriteError,     // A low-level write error occured (USB / socket layer)
            ReadError,      // A low-level read error occured (USB / socket layer)
            CommError,      // A low-level communication error occured
            BusError,       // VME Bus error
            NoDevice,       // No controller-type specific device found
            DeviceIsOpen,   // Tried to open an already opened device
            Timeout,
            HostNotFound,
            InvalidIPAddress,
            UnexpectedAddressMode,
            HostLookupFailed,
            WrongControllerType,
            StdErrorCode,       // Used by the MVLC implementation
            UnsupportedCommand, // Used by vme_script::run_command()
        };

        VMEError()
        {}

        VMEError(ErrorType error)
            : m_error(error)
        {}

        VMEError(ErrorType error, const QString &message)
            : m_error(error)
            , m_message(message)
        {}

        VMEError(const QString &message)
            : m_error(ErrorType::UnknownError)
            , m_message(message)
        {}

        VMEError(ErrorType error, s32 code)
            : m_error(error)
            , m_errorCode(code)
        {}

        VMEError(ErrorType error, s32 code, const QString &message)
            : m_error(error)
            , m_errorCode(code)
            , m_message(message)
        {}

        VMEError(ErrorType error, s32 code, const QString &message, const QString &codeString)
            : m_error(error)
            , m_errorCode(code)
            , m_message(message)
            , m_errorCodeString(codeString)
        {}

        VMEError(const std::error_code &ec)
            : m_error(StdErrorCode)
            , m_stdErrorCode(ec)
        {}

        inline bool isError() const
        {
            if (m_error == StdErrorCode)
                return static_cast<bool>(getStdErrorCode());

            return m_error != NoError;
        }

        inline bool isTimeout() const { return m_error == Timeout; }

        // Returns this errors type.
        inline ErrorType error() const { return m_error; }

        // Returns an implementation defined error message. Defaults to an empty string.
        inline QString message() const { return m_message; }
        void setMessage(const QString &message) { m_message = message; }

        // Returns an implementation defined error code. Defaults to 0.
        inline s32 errorCode() const { return m_errorCode; }

        QString toString() const;

        QString errorName() const;
        static QString errorName(ErrorType type);

        std::error_code getStdErrorCode() const { return m_stdErrorCode; }

    private:
        ErrorType m_error = NoError;
        s32 m_errorCode = 0;
        QString m_message;
        QString m_errorCodeString;
        std::error_code m_stdErrorCode;
};

class LIBMVME_CORE_EXPORT VMEController: public QObject
{
    Q_OBJECT
    signals:
        void controllerOpened();
        void controllerClosed();
        void controllerStateChanged(ControllerState state);

    public:
        VMEController(QObject *parent = 0);
        virtual ~VMEController() {}

        virtual VMEControllerType getType() const = 0;

        virtual VMEError write32(u32 address, u32 value, u8 amod) = 0;
        virtual VMEError write16(u32 address, u16 value, u8 amod) = 0;

        virtual VMEError read32(u32 address, u32 *value, u8 amod) = 0;
        virtual VMEError read16(u32 address, u16 *value, u8 amod) = 0;

        virtual VMEError blockRead(u32 address, u32 transfers, QVector<u32> *dest, u8 amod, bool fifo) = 0;

        virtual bool isOpen() const = 0;
        virtual VMEError open() = 0;
        virtual VMEError close() = 0;

        virtual ControllerState getState() const = 0;

        virtual QString getIdentifyingString() const = 0;
};

QString LIBMVME_CORE_EXPORT to_string(VMEControllerType type);
VMEControllerType LIBMVME_CORE_EXPORT from_string(const QString &str);

QString LIBMVME_CORE_EXPORT to_string(ControllerState state);

#endif // VMECONTROLLER_H
