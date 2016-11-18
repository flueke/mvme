#include "vmusb_stack.h"
#include "vmusb.h"
#include "CVMUSBReadoutList.h"

#include <QTextStream>
#include <QDebug>

size_t VMUSBStack::loadOffset = 0;

VMEError VMUSBStack::loadStack(VMUSB *vmusb)
{
    auto contents = getContents();

    qDebug("VMUSBStack::loadStack(): id=%u, loadOffset=%u, length=%u",
           getStackID(), loadOffset, contents.size());

    for (u32 line: contents)
    {
        qDebug("  0x%08x", line);
    }
    qDebug("----- End of stack -----");

    VMEError result;

    if (contents.size())
    {
        result = vmusb->stackWrite(getStackID(), loadOffset, contents);
        if (!result.isError())
        {
            // Stack size in 16-bit words + 4 for the stack header (from nscldaqs CStack)
            loadOffset += contents.size() * 2 + 4;
        }
    }
    return result;
}

VMEError VMUSBStack::enableStack(VMUSB *controller)
{
    auto stackID = getStackID();
    VMEError result;

    switch (triggerCondition)
    {
        case TriggerCondition::NIM1:
            {
#if 0
                uint32_t daqSettings = controller->getDaqSettings();
                daqSettings &= ~DaqSettingsRegister::ReadoutTriggerDelayMask;
                daqSettings |= readoutTriggerDelay << DaqSettingsRegister::ReadoutTriggerDelayShift;
                result = controller->setDaqSettings(daqSettings);
#endif
            } break;

        case TriggerCondition::Periodic:
            {
                uint32_t daqSettings = controller->getDaqSettings();
                daqSettings &= ~DaqSettingsRegister::ScalerReadoutFrequencyMask;
                daqSettings |= scalerReadoutFrequency << DaqSettingsRegister::ScalerReadoutFrequencyShift;

                daqSettings &= ~DaqSettingsRegister::ScalerReadoutPerdiodMask;
                daqSettings |= scalerReadoutPeriod << DaqSettingsRegister::ScalerReadoutPerdiodShift;

                result = controller->setDaqSettings(daqSettings);
            } break;

        case TriggerCondition::Interrupt:
            {
                uint16_t isvValue = (stackID << ISVWord::stackIDShift)
                    | (irqLevel << ISVWord::irqLevelShift)
                    | irqVector;

                int vectorNumber = stackID - 2;

                Q_ASSERT(0 <= vectorNumber && vectorNumber < 8);

                qDebug() << this << "enableStack: id=" << getStackID() << ", irqLevel=" << irqLevel << ", irqVector=" << irqVector
                    << ", vectorNumber(register)=" << vectorNumber;

                result = controller->setIrq(vectorNumber, isvValue);

#if 0
                uint32_t daqSettings = controller->getDaqSettings();
                daqSettings &= ~DaqSettingsRegister::ReadoutTriggerDelayMask;
                daqSettings |= readoutTriggerDelay << DaqSettingsRegister::ReadoutTriggerDelayShift;
                result = controller->setDaqSettings(daqSettings);
#endif
            } break;
    };

    return result;
}
