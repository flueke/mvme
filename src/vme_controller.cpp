#include "vme_controller.h"
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

            /* Using a union to reinterpret the float as an int as the
             * straight-forward way results in a warning:
             * u32 value = *reinterpret_cast<u32 *>(&floatValue);
             * // warning: dereferencing type-punned pointer will break strict-aliasing rules
             */
            union {
                float    floatValue;
                uint32_t intValue;
            } u = { floatValue };

            u32 value = u.intValue;

            switch (width)
            {
                case RegisterWidth::W16:
                    {
                        u16 regVal = 0;
                        // write low address
                        regVal = (value >> 16) & 0xFFFF;
                        result = write16(address, regVal, amod);

                        if (result <= 0)
                            break;

                        // write high address
                        regVal = (value & 0xFFFF);
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
