#ifndef VMECONTROLLER_H
#define VMECONTROLLER_H

#include "util.h"
#include "vme.h"
#include <QObject>

class VMECommandList;

enum class VMEControllerType
{
    VMUSB,
    CAEN,
    SIS
};

enum class RegisterWidth
{
    W16,
    W32
};

enum class ControllerState
{
    Closed,
    Opened
};

Q_DECLARE_METATYPE(ControllerState);

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

        virtual void write32(u32 address, u8 amod, u32 value) = 0;
        virtual void write16(u32 address, u8 amod, u16 value) = 0;

        virtual u32 read32(u32 address, u8 amod) = 0;
        virtual u16 read16(u32 address, u8 amod) = 0;

        virtual ssize_t executeCommands(VMECommandList *commands, void *readBuffer, size_t readBufferSize) = 0;

        virtual bool isOpen() const = 0;
        virtual bool openFirstDevice() = 0;
        virtual void close() = 0;

        virtual void applyRegisterList(const RegisterList &registerList,
                                 u32 baseAddress = 0,
                                 int writeDelayMS = 10,
                                 RegisterWidth width = RegisterWidth::W16,
                                 u8 amod = VME_AM_A32_USER_DATA);

        virtual ControllerState getState() const = 0;
};

#endif // VMECONTROLLER_H
