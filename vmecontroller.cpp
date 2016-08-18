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
        u32 value   = regVal.second;
        switch (width)
        {
            case RegisterWidth::W16:
                result = write16(address, value, amod);
                break;
            case RegisterWidth::W32:
                result = write32(address, value, amod);
                break;
        }
        if (result < 0)
            break;
        QThread::msleep(writeDelayMS);
    }

    return result;
}
