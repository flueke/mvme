#ifndef VMECONTROLLER_H
#define VMECONTROLLER_H

#include "vme.h"
#include "globals.h"
#include <QObject>

enum class VMEControllerType
{
    VMUSB,
    CAEN,
    SIS
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

        VMEError(ErrorType error, s32 code)
            : m_error(error)
            , m_errorCode(code)
        {} 

        VMEError(ErrorType error, s32 code, const QString &message)
            : m_error(error)
            , m_errorCode(code)
            , m_message(message)
        {}

        bool isError() const { return m_error != NoError; }

        // Returns this errors type.
        ErrorType error() const { return m_error; }

        // Returns an implementation defined error message. Defaults to an empty string.
        QString message() const { return m_message; }

        // Returns an implementation defined error code. Defaults to 0.
        s32 errorCode() const { return m_errorCode; }

        QString toString() const;

        QString errorName() const;
        static QString errorName(ErrorType type);

    private:
        ErrorType m_error = NoError;
        s32 m_errorCode = 0;
        QString m_message;
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
        virtual VMEError openFirstDevice() = 0;
        virtual VMEError close() = 0;

        virtual ControllerState getState() const = 0;
};

#endif // VMECONTROLLER_H
