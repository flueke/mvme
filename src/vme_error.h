#ifndef __MVME_VME_ERROR_H__
#define __MVME_VME_ERROR_H__

#include <QString>
#include <system_error>
#include "typedefs.h"

#include "libmvme_export.h"

/* VME Controller errors and results
 * ---------------------------------
 * - Types of operations:
 *   Controller level: open, close, low-level read write (usb, ...), timeout
 *      Result: success or error code + error message (controller specific)
 *   VME level: write32, bltRead, ...
 *      Result: vme data for read operations, berr (no dtack) error for write operations
 *
 */

class LIBMVME_EXPORT VMEError
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

        explicit VMEError(ErrorType error)
            : m_error(error)
        {}

        VMEError(ErrorType error, const QString &message)
            : m_error(error)
            , m_message(message)
        {}

        explicit VMEError(const QString &message)
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

        explicit VMEError(const std::error_code &ec)
            : m_error(StdErrorCode)
            , m_stdErrorCode(ec)
        {}

        inline bool isError() const
        {
            if (isWarning())
                return false;

            if (m_error == StdErrorCode)
                return static_cast<bool>(getStdErrorCode());

            return m_error != NoError;
        }

        inline bool isWarning() const
        {
            return m_isWarning;
        }

        inline void setIsWarning(bool isWarning)
        {
            m_isWarning = isWarning;
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

        // Returns true if this represents an error.
        explicit inline operator bool() const
        {
            return isError();
        }

    private:
        ErrorType m_error = NoError;
        s32 m_errorCode = 0;
        QString m_message;
        QString m_errorCodeString;
        std::error_code m_stdErrorCode;
        bool m_isWarning = false;
};


#endif /* __MVME_VME_ERROR_H__ */
