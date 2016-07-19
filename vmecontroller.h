#ifndef VMECONTROLLER_H
#define VMECONTROLLER_H

#include <cstdint>
#include <cstddef>

class VMECommandList;

class VMEController
{
    public:
        virtual ~VMEController() {}
        virtual void write32(uint32_t address, uint8_t amod, uint32_t value) = 0;
        virtual void write16(uint32_t address, uint8_t amod, uint16_t value) = 0;

        virtual uint32_t read32(uint32_t address, uint8_t amod) = 0;
        virtual uint16_t read16(uint32_t address, uint8_t amod) = 0;

        virtual size_t executeCommands(VMECommandList *commands, void *readBuffer, size_t readBufferSize) = 0;
};

#endif // VMECONTROLLER_H
