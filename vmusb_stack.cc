#include "vmusb_stack.h"
#include "vmusb.h"
#include "CVMUSBReadoutList.h"

size_t VMUSBStack::loadOffset = 0;

void VMUSBStack::loadStack(VMUSB *controller)
{
    VMECommandList readoutCommands;
    addReadoutCommands(&readoutCommands);
    CVMUSBReadoutList vmusbList(readoutCommands);

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

    switch (getTriggerType())
    {
        case NIM1:
            {
                Q_ASSERT(!"Not implemented");
            } break;
        case Scaler:
            {
                Q_ASSERT(!"Not implemented");
            } break;
        case Interrupt:
            {
                uint16_t isvValue = (stackID << ISVWord::stackIDShift)
                    | (irqLevel << ISVWord::irqLevelShift)
                    | irqVector;

                int vectorNumber = stackID;

                Q_ASSERT(0 <= vectorNumber && vectorNumber < 8);

                controller->setIrq(vectorNumber, isvValue);
            } break;
    };
}
