#ifndef UUID_d783c880_21d1_4644_a26c_a70d9daa299e
#define UUID_d783c880_21d1_4644_a26c_a70d9daa299e

#include "vmemodule.h"
#include <stdexcept>

class vmUsb;

class VMUSB_Stack: public VMEModule
{
    public:
        enum TriggerType
        {
            NIM1,
            Scaler,
            Interrupt
        };

        void setTriggerType(TriggerType trigger)
        {
            m_trigger = trigger;
        }

        TriggerType getTriggerType() const
        {
            return m_trigger;
        }

        void setStackID(uint8_t stackID)
        {
            if (stackID > 7)
                throw std::runtime_error("stackID out of range (>7)");

            m_stackID = stackID;
        }

        uint8_t getStackID() const
        {
            switch (m_trigger)
            {
                case NIM1:
                    return 0;
                case Scaler:
                    return 1;
                case Interrupt:
                    return m_stackID;
            }
            return 0;
        }

        void loadStack(vmUsb *controller);
        void enableStack(vmUsb *controller);

        void addModule(VMEModule *module)
        {
            m_members.append(module);
        }

        virtual void resetModule(VMEController *controller)
        {
            for (auto module: m_members)
            {
                module->resetModule(controller);
            }
        }

        virtual void addInitCommands(VMECommandList *cmdList)
        {
            for (auto module: m_members)
            {
                module->addInitCommands(cmdList);
            }
        }

        virtual void addReadoutCommands(VMECommandList *cmdList)
        {
            for (auto module: m_members)
            {
                module->addReadoutCommands(cmdList);
            }
        }

        virtual void addStartDaqCommands(VMECommandList *cmdList)
        {
            for (auto module: m_members)
            {
                module->addStartDaqCommands(cmdList);
            }
        }

        virtual void addStopDaqCommands(VMECommandList *cmdList)
        {
            for (auto module: m_members)
            {
                module->addStopDaqCommands(cmdList);
            }
        }

        uint8_t irqLevel = 0;
        uint8_t irqVector = 0;
        static size_t loadOffset;
        
    private:
        TriggerType m_trigger = NIM1;
        uint8_t m_stackID = 0;
        QVector<VMEModule *> m_members;
};


#endif
