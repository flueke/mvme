#ifndef UUID_2e093e44_6f56_4619_8368_5b337f808db3
#define UUID_2e093e44_6f56_4619_8368_5b337f808db3

#include "util.h"
#include "vmecontroller.h"
#include "vmecommandlist.h"
#include <QMap>
#include <QVector>
#include <QString>
#include <QThread>
#include <QDebug>

struct MVMEContext
{
};

enum class VMEModuleTypes
{
    Unknown = 0,
    MADC = 1,
    MQDC = 2,
    MTDC = 3,
    MDPP16 = 4,
    MDPP32 = 5,
    MDI2 = 6
};

class VMEModule
{
    public:
        VMEModule(const QString &name = QString())
            : m_name(name)
        { }
        virtual ~VMEModule() {}
        virtual void resetModule(VMEController *controller) = 0;
        virtual void addInitCommands(VMECommandList *cmdList) = 0;
        virtual void addReadoutCommands(VMECommandList *cmdList) = 0;
        virtual void addStartDaqCommands(VMECommandList *cmdList) = 0;
        virtual void addStopDaqCommands(VMECommandList *cmdList) = 0;

        QString getName() const { return m_name; }
        void setName(const QString &name) { m_name = name; }

    private:
        QString m_name;
};

class HardwareModule: public VMEModule
{
    public:
        HardwareModule(uint32_t baseAddress = 0, const QString &name = QString())
            : VMEModule(name)
            , baseAddress(baseAddress)
        {}

        uint32_t baseAddress = 0;
};

class MesytecModule: public HardwareModule
{
    public:
        static const uint8_t registerAMod = 0x09;
        static const uint8_t bltAMod = 0x0b;
        static const uint8_t mbltAMod = 0x08;

        MesytecModule(uint32_t baseAddress = 0, uint8_t moduleID = 0xff, const QString &name = QString())
            : HardwareModule(baseAddress, name)
        {
            registerData[0x6004] = moduleID;
        }

        virtual void resetModule(VMEController *controller)
        {
            writeRegister(controller, 0x6008, 1);
            QThread::sleep(1);
        }

        virtual void addInitCommands(VMECommandList *cmdList)
        {
            for (uint16_t address: registerData.keys())
            {
                cmdList->addWrite16(baseAddress + address, registerAMod, registerData[address]);
            }
        }

        virtual void addReadoutCommands(VMECommandList *cmdList)
        {
            // TODO, FIXME: number of transfers?! depends on multi event mode
            cmdList->addFifoRead32(baseAddress, bltAMod, 128);
            cmdList->addWrite16(baseAddress + 0x6034, registerAMod, 1); // readout reset
        }

        virtual void addStartDaqCommands(VMECommandList *cmdList)
        {
            cmdList->addWrite16(baseAddress + 0x603c, registerAMod, 1); // FIFO reset
            cmdList->addWrite16(baseAddress + 0x6034, registerAMod, 1); // readout reset
            cmdList->addWrite16(baseAddress + 0x603a, registerAMod, 1); // start acq
        }

        virtual void addStopDaqCommands(VMECommandList *cmdList)
        {
            cmdList->addWrite16(baseAddress + 0x603a, registerAMod, 0); // stop acq
        }

        void writeRegister(VMEController *controller, uint16_t address, uint16_t value)
        {
            controller->write16(baseAddress + address, registerAMod, value);
        }

        void readRegister(VMEController *controller, uint16_t address)
        {
            controller->read16(baseAddress + address, registerAMod);
        }

        void setIrqLevel(uint8_t irqLevel)
        {
            registerData[0x6010] = irqLevel;
        }

        void setIrqVector(uint8_t irqVector)
        {
            registerData[0x6012] = irqVector;
        }

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

class MDPP16: public MesytecModule
{
    public:
        MDPP16(uint32_t baseAddress = 0, uint8_t moduleID = 0xff)
            : MesytecModule(baseAddress, moduleID)
        {}
};

class MADC32: public MesytecModule
{
    public:
        MADC32(uint32_t baseAddress = 0, uint8_t moduleID = 0xff)
            : MesytecModule(baseAddress, moduleID)
        {}
};

#if 0
class MQDC32: public MesytecModule
{
};

class MTDC32: public MesytecModule
{
};
#endif

#if 0
void foo()
{

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

}
#endif
#endif
