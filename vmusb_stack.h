#ifndef UUID_d783c880_21d1_4644_a26c_a70d9daa299e
#define UUID_d783c880_21d1_4644_a26c_a70d9daa299e

#include "vme_module.h"
#include "globals.h"
#include <stdexcept>

class VMUSB;

class VMUSBStack: public VMEModule
{
    public:
        void setStackID(uint8_t stackID)
        {
            if (stackID > 7)
                throw std::runtime_error("stackID out of range (>7)");

            m_stackID = stackID;
        }

        uint8_t getStackID() const
        {
            switch (triggerCondition)
            {
                case TriggerCondition::NIM1:
                    return 0;
                case TriggerCondition::Scaler:
                    return 1;
                case TriggerCondition::Interrupt:
                    return m_stackID;
            }
            return 0;
        }

        void loadStack(VMUSB *controller);
        void enableStack(VMUSB *controller);

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

        /* Reset the global load offset. Use between runs. */
        void resetLoadOffset()
        {
            loadOffset = 0;
        }

        static size_t loadOffset;

        TriggerCondition triggerCondition;
        uint8_t irqLevel = 0;
        uint8_t irqVector = 0;
        // Maximum time between scaler stack executions in units of 0.5s
        uint8_t scalerReadoutPeriod = 0;
        // Maximum number of events between scaler stack executions
        uint16_t scalerReadoutFrequency = 0;
        
    private:
        uint8_t m_stackID = 0;
        QVector<VMEModule *> m_members;
};


#endif
