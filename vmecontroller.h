#ifndef VMECONTROLLER_H
#define VMECONTROLLER_H

#include <QObject>

class VMECommandList;

enum class VMEControllerType
{
    VMUSB,
    CAEN,
    SIS
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

        virtual void write32(uint32_t address, uint8_t amod, uint32_t value) = 0;
        virtual void write16(uint32_t address, uint8_t amod, uint16_t value) = 0;

        virtual uint32_t read32(uint32_t address, uint8_t amod) = 0;
        virtual uint16_t read16(uint32_t address, uint8_t amod) = 0;

        virtual size_t executeCommands(VMECommandList *commands, void *readBuffer, size_t readBufferSize) = 0;

        virtual bool isOpen() const = 0;
};

#endif // VMECONTROLLER_H
