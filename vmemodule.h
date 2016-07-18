#ifndef UUID_2e093e44_6f56_4619_8368_5b337f808db3
#define UUID_2e093e44_6f56_4619_8368_5b337f808db3

#include "util.h"
#include <QMap>
#include <QVector>

struct VMECommand
{
    enum Type
    {
        NotSet,
        Write32,
        Write16,
        Read32,
        Read16,
        BlockRead32,
        FifoRead32,
        BlockCountRead16,
        BlockCountRead32,
        MaskedCountBlockRead32,
        MaskedCountFifoRead32,
        Delay,
        Marker,
    };

    uint32_t address;
    uint32_t value;
    uint8_t  amod;
    size_t transfers;
};

class VMECommandList
{
    public:
        void addWrite32(uint32_t address, uint8_t amod, uint32_t value);
        void addWrite16(uint32_t address, uint8_t amod, uint16_t value);

        void addRead32(uint32_t address, uint8_t amod);
        void addRead16(uint32_t address, uint8_t amod);

        void addBlockRead32(uint32_t baseAddress, uint8_t amod, size_t transfers);
        void addFifoRead32(uint32_t  baseAddress, uint8_t amod, size_t transfers);

        void addBlockCountRead16(uint32_t address, uint32_t mask, uint8_t amod);
        void addBlockCountRead32(uint32_t address, uint32_t mask, uint8_t amod);

        void addMaskedCountBlockRead32(uint32_t address, uint8_t amod);
        void addMaskedCountFifoRead32(uint32_t address, uint8_t amod);

        void addDelay(uint8_t _200nsClocks);
        void addMarker(uint16_t value);

        QVector<VMECommand> commands;
};

class VMEController
{
    public:
        virtual ~VMEController();
        virtual void write32(uint32_t address, uint8_t amod, uint32_t value) = 0;
        virtual void write16(uint32_t address, uint8_t amod, uint16_t value) = 0;

        virtual uint32_t read32(uint32_t address, uint8_t amod) = 0;
        virtual uint16_t read16(uint32_t address, uint8_t amod) = 0;
};

class VMEModule
{
    public:
        virtual ~VMEModule() {}
        virtual void resetModule(VMEController *controller) = 0;
        virtual void addInitCommands(VMECommandList *cmdList) = 0;
        virtual void addReadoutCommands(VMECommandList *cmdList) = 0;
        virtual void addStartDAQCommands(VMECommandList *cmdList) = 0;
        virtual void addStopDAQCommands(VMECommandList *cmdList) = 0;
};

class HardwareModule: public VMEModule
{
    public:
        HardwareModule(uint32_t baseAddress = 0)
            : baseAddress(baseAddress)
        {}

        uint32_t baseAddress = 0;
};

class MesytecModule: public HardwareModule
{
    public:
        static const uint8_t registerAMod = 0x09;
        static const uint8_t bltAMod = 0x0b;
        static const uint8_t mbltAMod = 0x08;

        MesytecModule(u32 baseAddress = 0, uint8_t moduleID = 0xff);

        virtual void resetModule(VMEController *controller) {}
        virtual void addInitCommands(VMECommandList *cmdList) {}
        virtual void addReadoutCommands(VMECommandList *cmdList) {}
        virtual void addStartDAQCommands(VMECommandList *cmdList) {}
        virtual void addStopDAQCommands(VMECommandList *cmdList) {}

        QMap<uint16_t, uint16_t> registerData;
};

class MesytecChain: public VMEModule
{
    public:
        MesytecChain(uint8_t cblt_address, uint8_t mcst_address);
        QVector<MesytecModule *> members;
        uint8_t cblt_address;
        uint8_t mcst_address;
};

class Stack: public VMEModule
{
    public:
        QVector<VMEModule *> members;
};

class MDPP16: public MesytecModule
{
};

class MADC32: public MesytecModule
{
};

class MQDC32: public MesytecModule
{
};

class MTDC32: public MesytecModule
{
};

void foo()
{

#if 1
    auto mdpp16 = new MDPP16(0x0);
    mdpp16->setRegister(0x6030, 1);
    mdpp16->loadInitList("mdpp16.init");
    mdpp16->setIrqPriority(1); // 1-7, 0=disable
    mdpp16->setIrqVector(0);  // 0-255
    // ....

    auto madc32 = new MADC32(0x1);
    madc32->setRegister(0x6030, 1);

    auto controller = new vmUsb();
    controller->open();

    QVector<VMEModule *> modules;
    modules << mdpp16 << madc32;

    auto stack = new VMUSBStack;
    stack->addModule(mdpp16);
    stack->addModule(madc32);
    // same as for mdpp16
    stack->setIrqPriority(1);
    stack->setIrqVector(0);

    //for (auto module: modules)
    //{
    //    // module knows init order and can use basic vme operations to init itself (how to make use of readout lists?)
    //    module->init(controller);
    //    irq = module->getIrq();
    //    module->addReadoutCommands(readoutLists[irq-1]);
    //}

    // load list to stack id 0, mem offset 0
    controller->listLoad(&readoutList, 0, 0, 200);
    controller->set

    for (auto module: modules)
    {
        // module knows what to do when starting datataking
        module->startDatataking(controller);
    }

    controller->startDaq();
    while (true)
    {
        controller->daqRead();
    }
    controller->stopDaq();
#endif
}

#endif
