#ifndef UUID_2e093e44_6f56_4619_8368_5b337f808db3
#define UUID_2e093e44_6f56_4619_8368_5b337f808db3

#include <util.h>

class VMEReadoutList;
class VMEController;

class AbstractVMEModule
{
    public:
        virtual ~AbstractVMEModule() {}
        virtual void init(VMEController *controller) = 0;
        virtual void addReadoutCommands(VMEReadoutList *readoutList) = 0;
};

class VMEModule: public AbstractVMEModule
{
    public:
        VMEModule(u32 baseAddress = 0)
            : m_baseAddress(baseAddress)
        {}

        virtual void init(VMEController *controller) = 0;
        virtual void addReadoutCommands(CVMUSBReadoutList *readoutList) = 0;

        u32 m_baseAddress = 0;
};

class MesytecModule: public VMEModule
{
    public:
        MesytecModule(u32 baseAddress = 0, uint8_t moduleID = 0xff);

    QMap<uint16_t, uint16_t> registerData;
};

class MesytecChain: public AbstractVMEModule
{
    public:
        QVector<MesytecModule *> members;
};

class Stack: public AbstractVMEModule
{
    public:
        QVector<AbstractVMEModule *> members;
};

#endif
