#include "vmusb_stack.h"
#include "vmusb.h"
#include "CVMUSBReadoutList.h"

#include <QTextStream>

size_t VMUSBStack::loadOffset = 0;

void VMUSBStack::loadStack(VMUSB *controller)
{
    VMECommandList readoutCommands;
    addReadoutCommands(&readoutCommands);
    CVMUSBReadoutList vmusbList(readoutCommands);

    QString tmp;
    QTextStream strm(&tmp);
    readoutCommands.dump(strm);
    qDebug() << this << "loadStack: id=" << getStackID() << ", loadOffset =" << loadOffset << endl << tmp << endl;

    if (vmusbList.size())
    {
        controller->listLoad(&vmusbList, getStackID(), loadOffset);
        // Stack size in words + 4 for the stack header (from nscldaqs CStack)
        loadOffset += vmusbList.size() * 2 + 4;
    }
}

void VMUSBStack::enableStack(VMUSB *controller)
{
    auto stackID = getStackID();

    switch (triggerCondition)
    {
        case TriggerCondition::NIM1:
            {
            } break;
        case TriggerCondition::Scaler:
            {
                uint32_t daqSettings = controller->getDaqSettings();
                daqSettings &= ~DaqSettingsRegister::ScalerReadoutFrequencyMask;
                daqSettings |= scalerReadoutFrequency << DaqSettingsRegister::ScalerReadoutFrequencyShift;

                daqSettings &= ~DaqSettingsRegister::ScalerReadoutPerdiodMask;
                daqSettings |= scalerReadoutPeriod << DaqSettingsRegister::ScalerReadoutPerdiodShift;

                controller->setDaqSettings(daqSettings);
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

                controller->setIrq(vectorNumber, isvValue);
            } break;
    };
}
