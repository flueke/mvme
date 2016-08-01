#ifndef UUID_d783c880_21d1_4644_a26c_a70d9daa299e
#define UUID_d783c880_21d1_4644_a26c_a70d9daa299e

#include "globals.h"
#include "mvme_config.h"
#include "vmecommandlist.h"
#include <stdexcept>

class VMUSB;

class VMUSBStack
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

        void addModule(ModuleConfig *module)
        {
            m_modules.append(module);
        }

#if 0
        // XXX: this used to call module->addReadoutCommands()
        // With the new template scheme this does not work anymore as now the
        // stack contents for each module is just a QVector<u32>
        VMECommandList getReadoutCommands() const
        {
            VMECommandList ret;
            for (auto module: m_modules)
            {
                auto stack = parseStackFile(module->readoutStack);

                ret.append(VMECommandList::fromInitList(
                        parseStackFile(module->readoutStack),
                        module->baseAddress));
            }
            return ret;
        }
#endif
        QVector<u32> getContents() const
        {
            QVector<u32> result;
            for (auto module: m_modules)
            {
                result += parseStackFile(module->readoutStack);
            }

            return result;
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
        uint8_t m_stackID = 2;
        QVector<ModuleConfig *> m_modules;
};


#endif
