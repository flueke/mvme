#include "vmedevice.h"
#include "mvme.h"

VmeDevice::VmeDevice(QObject *parent) :
    QObject(parent)
{
    m_myMvme = (mvme*)parent;
    m_baseAddress = 0x0;
    m_deviceName.sprintf("VME device");
    m_moduleId = 0;
    m_resolution = 8192;
}

void VmeDevice::setModId(quint16 id)
{
}

