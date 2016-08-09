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

class VMEController: public QObject
{
    Q_OBJECT
    signals:
        void controllerOpened();
        void controllerClosed();

    public:
        VMEController(QObject *parent = 0);
        virtual ~VMEController() {}
        virtual VMEControllerType getType() const = 0;

        virtual void write32(u32 address, u8 amod, u32 value) = 0;
        virtual void write16(u32 address, u8 amod, u16 value) = 0;

        virtual u32 read32(u32 address, u8 amod) = 0;
        virtual u16 read16(u32 address, u8 amod) = 0;

        virtual size_t executeCommands(VMECommandList *commands, void *readBuffer, size_t readBufferSize) = 0;


        virtual bool isOpen() const = 0;

        virtual void applyRegisterList(const RegisterList &registerList,
                                 u32 baseAddress = 0,
                                 int writeDelayMS = 10,
                                 RegisterWidth width = RegisterWidth::W16,
                                 u8 amod = VME_AM_A32_USER_DATA);
};

#endif // VMECONTROLLER_H
