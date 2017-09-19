/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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

#include "vme.h"
#include "globals.h"
#include <QObject>

enum class VMEControllerType
{
    VMUSB,
    SIS3153
};

enum class ControllerState
{
    Unknown,
    Closed,
    Opened
};

Q_DECLARE_METATYPE(ControllerState);


/* VME Controller errors and results
 * ---------------------------------
 * - Types of operations:
 *   Controller level: open, close, low-level read write (usb, ...), timeout
 *      Result: success or error code + error message (controller specific)
 *   VME level: write32, bltRead, ...
 *      Result: vme data for read operations, berr (no dtack) error for write operations
 *
 */

class VMEError
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
            InvalidIPAddress
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

        inline bool isError() const { return m_error != NoError; }
        inline bool isTimeout() const { return m_error == Timeout; }

        // Returns this errors type.
        inline ErrorType error() const { return m_error; }

        // Returns an implementation defined error message. Defaults to an empty string.
        inline QString message() const { return m_message; }

        // Returns an implementation defined error code. Defaults to 0.
        inline s32 errorCode() const { return m_errorCode; }

        QString toString() const;

        QString errorName() const;
        static QString errorName(ErrorType type);

    private:
        ErrorType m_error = NoError;
        s32 m_errorCode = 0;
        QString m_message;
        QString m_errorCodeString;
};

class VMEController: public QObject
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

QString to_string(VMEControllerType type);
VMEControllerType from_string(const QString &str);

#endif // VMECONTROLLER_H
