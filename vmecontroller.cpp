#include "vmecontroller.h"
#include <QThread>

VMEController::VMEController(QObject *parent)
    : QObject(parent)
{}

int VMEController::applyRegisterList(const RegisterList &regList,
                                      u32 baseAddress,
                                      int writeDelayMS,
                                      RegisterWidth width,
                                      u8 amod)
{
    int result = 0;

    for (auto regVal: regList)
    {
        u32 address = regVal.first + baseAddress;

        if (isFloat(regVal.second))
        {
            float floatValue = regVal.second.toFloat();
            u32 value = *reinterpret_cast<u32 *>(&floatValue);

            switch (width)
            {
                case RegisterWidth::W16:
                    {
                        // write the low bytes to the lower address
                        u16 regVal = (value & 0xFFFF);
                        result = write16(address, regVal, amod);

                        if (result <= 0)
                            break;

                        // high bytes to address + 16 bits
                        regVal = (value >> 16) & 0xFFFF;
                        result = write16(address + sizeof(u16), regVal, amod);
                    } break;

                case RegisterWidth::W32:
                    result = write32(address, value, amod);
                    break;
            }
        }
        else
        {
            u32 value = regVal.second.toUInt();
            switch (width)
            {
                case RegisterWidth::W16:
                    result = write16(address, value, amod);
                    break;
                case RegisterWidth::W32:
                    result = write32(address, value, amod);
                    break;
            }
        }
        if (result < 0)
            break;
        QThread::msleep(writeDelayMS);
    }

    return result;
}
