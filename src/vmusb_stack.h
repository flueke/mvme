/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef UUID_d783c880_21d1_4644_a26c_a70d9daa299e
#define UUID_d783c880_21d1_4644_a26c_a70d9daa299e

#include "globals.h"
#include "vme_config.h"
#include <stdexcept>

class VMUSB;

class VMUSBStack
{
    public:
        void setStackID(s16 stackID)
        {
            if (stackID < 0)
                throw std::runtime_error("stackID out of range (<0)");

            if (stackID > 7)
                throw std::runtime_error("stackID out of range (>7)");

            m_stackID = stackID;
        }

        s16 getStackID() const
        {
            switch (triggerCondition)
            {
                case TriggerCondition::NIM1:
                    return 0;
                case TriggerCondition::Periodic:
                    return 1;
                case TriggerCondition::Interrupt:
                    return m_stackID;
                default:
                    break;
            }
            return -1;
        }

        VMEError loadStack(VMUSB *controller);
        VMEError enableStack(VMUSB *controller);

        void setContents(const QVector<u32> contents) { m_contents = contents; }
        QVector<u32> getContents() const { return m_contents; }

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
        static const s16 FirstIRQStackID = 2;
        s16 m_stackID = FirstIRQStackID;
        QVector<u32> m_contents;
};


#endif
