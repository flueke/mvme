#include "vmusb_stack.h"
#include "vmusb.h"
#include "CVMUSBReadoutList.h"

size_t VMUSB_Stack::loadOffset = 0;

void VMUSB_Stack::loadStack(vmUsb *controller)
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

void VMUSB_Stack::enableStack(vmUsb *controller)
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

                int vectorNumber = stackID - 2;

                Q_ASSERT(0 <= vectorNumber && vectorNumber < 8);

                controller->setIrq(vectorNumber, isvValue);
            } break;
    };
}
