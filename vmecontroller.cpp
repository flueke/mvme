#include "vmecontroller.h"
#include <QThread>

VMEController::VMEController(QObject *parent)
    : QObject(parent)
{}

void VMEController::applyRegisterList(const RegisterList &regList,
                                      u32 baseAddress,
                                      int writeDelayMS,
                                      RegisterWidth width,
                                      u8 amod)
{
    for (auto regVal: regList)
    {
        u32 address = regVal.first + baseAddress;
        u32 value   = regVal.second;
        switch (width)
        {
            case RegisterWidth::W16:
                write16(address, amod, value);
                break;
            case RegisterWidth::W32:
                write32(address, amod, value);
                break;
        }
        QThread::msleep(writeDelayMS);
    }
}
