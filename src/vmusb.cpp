/***************************************************************************
 *   Copyright (C) 2014 by Gregor Montermann                               *
 *   g.montermann@mesytec.com                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "vmusb.h"
#include "CVMUSBReadoutList.h"
#include "vme.h"
#include <QDebug>
#include <QMutexLocker>

#ifdef WIENER_USE_LIBUSB1
#include <libusb.h>
#else
    #ifdef __MINGW32__
        #include <lusb0_usb.h>
    #else
        #include <usb.h>
    #endif
#endif

#define XXUSB_WIENER_VENDOR_ID  0x16DC  /* Wiener, Plein & Baus */
#define XXUSB_VMUSB_PRODUCT_ID  0x000B  /* VM-USB */

////////////////////////////////////////////////////////////////////////
/*
   Build up a packet by adding a 16 bit word to it;
   the datum is packed low endianly into the packet.

*/
static void* addToPacket16(void* packet, uint16_t datum)
{
    uint8_t* pPacket = static_cast<uint8_t*>(packet);

    *pPacket++ = (datum  & 0xff); // Low byte first...
    *pPacket++ = (datum >> 8) & 0xff; // then high byte.

    return static_cast<void*>(pPacket);
}

/////////////////////////////////////////////////////////////////////////
/*
  Build up a packet by adding a 32 bit datum to it.
  The datum is added low-endianly to the packet.
*/
static void* addToPacket32(void* packet, uint32_t datum)
{
    uint8_t* pPacket = static_cast<uint8_t*>(packet);

    *pPacket++    = (datum & 0xff);
    *pPacket++    = (datum >> 8) & 0xff;
    *pPacket++    = (datum >> 16) & 0xff;
    *pPacket++    = (datum >> 24) & 0xff;

    return static_cast<void*>(pPacket);
}

static QString errnoString(int errnum)
{
#ifdef __MINGW32__
    return QString(strerror(errnum));
#else
    char buffer[256] = {};
    return QString(strerror_r(errnum, buffer, sizeof(buffer)));
#endif
}

struct VMUSBOpenResult
{
    int errorCode;
#ifdef WIENER_USE_LIBUSB1
    libusb_device_handle *deviceHandle;
#else
    usb_dev_handle *deviceHandle;
#endif
};

#ifdef WIENER_USE_LIBUSB1
static VMUSBOpenResult open_usb_device(libusb_device *dev)
{
    Q_ASSERT(dev);

    VMUSBOpenResult result = {};

    result.errorCode = libusb_open(dev, &result.deviceHandle);

    if (result.errorCode != 0)
    {
        qDebug() << "libusb_open failed"
            << libusb_strerror(static_cast<libusb_error>(result.errorCode));
        return result;
    }

    result.errorCode = libusb_set_configuration(result.deviceHandle, 1);

    if (result.errorCode != 0)
    {
        qDebug() << "libusb_set_configuration failed"
            << libusb_strerror(static_cast<libusb_error>(result.errorCode));

        libusb_close(result.deviceHandle);
        return result;
    }

    result.errorCode = libusb_claim_interface(result.deviceHandle, 0);

    if (result.errorCode != 0)
    {
        qDebug() << "libusb_claim_interface failed"
            << libusb_strerror(static_cast<libusb_error>(result.errorCode));

        libusb_close(result.deviceHandle);
        return result;
    }

    return result;
}

static int close_usb_device(libusb_device_handle *deviceHandle)
{
    Q_ASSERT(deviceHandle);

    int result = libusb_release_interface(deviceHandle, 0);

    if (result != 0)
    {
        qDebug() << "libusb_release_interface failed:"
            << libusb_strerror(static_cast<libusb_error>(result));
        return result;
    }

    libusb_close(deviceHandle);

    return result;
}
#else
static VMUSBOpenResult open_usb_device(struct usb_device *dev)
{
    Q_ASSERT(dev);

    VMUSBOpenResult result = {};

    result.deviceHandle = usb_open(dev);

    if (!result.deviceHandle)
    {
        result.errorCode = -1;
        return result;
    }

    result.errorCode = usb_set_configuration(result.deviceHandle, 1);

    if (result.errorCode != 0)
    {
        qDebug() << "usb_set_configuration failed";

        usb_close(result.deviceHandle);
        return result;
    }

    result.errorCode = usb_claim_interface(result.deviceHandle, 0);

    if (result.errorCode != 0)
    {
        qDebug() << "usb_claim_interface failed";

        usb_close(result.deviceHandle);
        return result;
    }

    return result;
}

static int close_usb_device(usb_dev_handle *deviceHandle)
{
    Q_ASSERT(deviceHandle);

    int result = usb_release_interface(deviceHandle, 0);

    if (result != 0)
    {
        qDebug() << "usb_release_interface failed";
        return result;
    }

    usb_close(deviceHandle);

    return result;
}
#endif

VMUSB::VMUSB()
    : m_state(ControllerState::Disconnected)
    , m_lock(QMutex::Recursive) // FIXME: make this non-recursive!
{
}


VMUSB::~VMUSB()
{
    close();

#ifdef WIENER_USE_LIBUSB1
    for (auto deviceInfo: m_deviceInfos)
    {
        libusb_unref_device(deviceInfo.device);
    }

    if (m_libusbContext)
    {
        libusb_exit(m_libusbContext);
    }
#endif
}

// TODO: add some sort of error reporting here
void VMUSB::getUsbDevices(void)
#ifdef WIENER_USE_LIBUSB1
{
    if (!m_libusbContext)
    {
        qDebug() << __PRETTY_FUNCTION__ << "libusb init";
        int result = libusb_init(&m_libusbContext);

        if (result != 0)
        {
            m_libusbContext = nullptr;
            return;
        }
        else
        {
            libusb_set_debug(m_libusbContext, LIBUSB_LOG_LEVEL_WARNING);
            //libusb_set_debug(m_libusbContext, LIBUSB_LOG_LEVEL_DEBUG);
        }
    }

    for (auto deviceInfo: m_deviceInfos)
    {
        libusb_unref_device(deviceInfo.device);
    }

    m_deviceInfos.clear();

    libusb_device **deviceList;

    ssize_t deviceCount = libusb_get_device_list(m_libusbContext, &deviceList);

    //qDebug() << __PRETTY_FUNCTION__ << "got a list of" << deviceCount << "usb devices";

    for (ssize_t deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
    {
        libusb_device *device = deviceList[deviceIndex];

        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(device, &descriptor) == 0
            && descriptor.idVendor == XXUSB_WIENER_VENDOR_ID
            && descriptor.idProduct == XXUSB_VMUSB_PRODUCT_ID)
        {
            qDebug() << __PRETTY_FUNCTION__ << "found a VMUSB device";

            libusb_device_handle *deviceHandle;
            int openResult = libusb_open(device, &deviceHandle);

            if (openResult == 0)
            {
                qDebug() << __PRETTY_FUNCTION__ << "was able to open the VMUSB";
                VMUSBDeviceInfo deviceInfo;

                if (libusb_get_string_descriptor_ascii(deviceHandle, descriptor.iSerialNumber,
                                                       deviceInfo.serial, sizeof(deviceInfo.serial)) >= 0)
                {
                    qDebug() << __PRETTY_FUNCTION__ << "was able to read the serial number:" << deviceInfo.serial;
                    deviceInfo.device = device;
                    // Increment the devices ref counter.
                    libusb_ref_device(device);
                    m_deviceInfos.push_back(deviceInfo);
                }
                libusb_close(deviceHandle);
            }
            else
            {
                qDebug() << __PRETTY_FUNCTION__ << "could not open the VMUSB:"
                    << libusb_strerror(static_cast<libusb_error>(openResult));
            }
        }
    }

    // Set the 'unref' parameter. Devices that we're interested in have been
    // manually referenced above.
    //qDebug() << "freeing the libusb device list";
    libusb_free_device_list(deviceList, 1);
}
#else
{
    struct usb_bus *bus;
    struct usb_device *dev;
    struct usb_bus *usb_busses_;

    m_deviceInfos.clear();

    //usb_set_debug(4);
    usb_init();
    usb_find_busses();
    usb_find_devices();
    usb_busses_=usb_get_busses();

    for (bus=usb_busses_; bus; bus = bus->next)
    {
        for (dev = bus->devices; dev; dev= dev->next)
        {
            //qDebug("descriptor: %d %d",dev->descriptor.idVendor, XXUSB_WIENER_VENDOR_ID);

            if (dev->descriptor.idVendor==XXUSB_WIENER_VENDOR_ID
                && dev->descriptor.idProduct == XXUSB_VMUSB_PRODUCT_ID)
            {
                qDebug("device scan: found wiener device");
                usb_dev_handle *udev = usb_open(dev);
                //qDebug("result of open: %d", udev);
                if (udev)
                {
                    qDebug("device scan: opened wiener device");

                    VMUSBDeviceInfo info;
                    info.device = dev;

                    int status = usb_get_string_simple(udev, dev->descriptor.iSerialNumber, info.serial, sizeof(info.serial));

                    if (status < 0)
                        info.serial[0] = '\0';

                    m_deviceInfos.push_back(info);

                    qDebug("serial=%s", info.serial);

                    usb_close(udev);
                }
            }
        }
    }
}
#endif

VMEError VMUSB::open()
{
    QMutexLocker locker(&m_lock);

    if (isOpen())
    {
        return VMEError(VMEError::DeviceIsOpen);
    }

    m_state = ControllerState::Connecting;
    emit controllerStateChanged(m_state);

    getUsbDevices();

    if (m_deviceInfos.isEmpty())
    {
        m_state = ControllerState::Disconnected;
        emit controllerStateChanged(m_state);
        return VMEError(VMEError::NoDevice);
    }

    auto deviceInfo = m_deviceInfos[0];

    VMUSBOpenResult openResult = open_usb_device(deviceInfo.device);

    if (openResult.errorCode == 0)
    {
        m_deviceHandle = openResult.deviceHandle;

        m_currentSerialNumber = reinterpret_cast<char *>(deviceInfo.serial);

        qDebug() << "opened VMUSB, serial =" << m_currentSerialNumber;

        writeActionRegister(0x04); // reset usb

        // clear the action register (makes sure daq mode is disabled)
        auto error = tryErrorRecovery();
        if (!error.isError())
        {
            m_state = ControllerState::Connected;
            emit controllerOpened();
            emit controllerStateChanged(m_state);
        }
        else
        {
            qDebug() << "vmusb error recovery failed:" << error.toString();
            close();
        }

        qDebug() << __PRETTY_FUNCTION__ << ">>>>>> post error recovery register dump";
        dump_registers(this, [](const QString &line) { qDebug().noquote() << " " << line; });
        qDebug() << __PRETTY_FUNCTION__ << "<<<<<< end of register dump";

        return error;
    }

#ifdef WIENER_USE_LIBUSB1
    return VMEError(VMEError::CommError, openResult.errorCode,
                    libusb_strerror(static_cast<libusb_error>(openResult.errorCode)),
                    libusb_error_name(openResult.errorCode));
#else
    return VMEError(VMEError::CommError, errnoString(errno));
#endif
}

VMEError VMUSB::close()
{
    QMutexLocker locker (&m_lock);
    if (m_deviceHandle)
    {
        close_usb_device(m_deviceHandle);
        m_deviceHandle = nullptr;
        m_currentSerialNumber = QString();
        m_state = ControllerState::Disconnected;
        emit controllerClosed();
        emit controllerStateChanged(m_state);
    }
    return {};
}

QString VMUSB::getIdentifyingString() const
{
    if (isOpen())
    {
        return QSL("VMUSB ") + m_currentSerialNumber;
    }

    return QSL("VMUSB");
}

VMEError VMUSB::readRegister(u32 address, u32 *value)
{
    size_t bytesRead = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRegisterRead(address);
    return listExecute(&readoutList, value, sizeof(*value), &bytesRead);
}

VMEError VMUSB::writeRegister(u32 address, u32 value)
{
    size_t bytesRead = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRegisterWrite(address, value);
    auto ret =  listExecute(&readoutList, &value, sizeof(value), &bytesRead);
    qDebug("writeRegister response: reg=0x%04x, value=0x%08x, bytes=%lu", address, value, bytesRead);
    return ret;
}

static const QMap<u32, QString> registerNames =
{
    { FIDRegister, "FirmwareId" },
    { GMODERegister, "GlobalMode" },
    { DAQSetRegister, "DAQSettings" },
    { LEDSrcRegister, "LEDSources" },
    { DEVSrcRegister, "DeviceSources" },
    { DGGARegister, "DGGASettings" },
    { DGGBRegister, "DGGBSettings" },
    { ScalerA, "ScalerAData" },
    { ScalerB, "ScalerBData" },
    { EventsPerBuffer, "EventsPerBuffer" },
    { ISV12, "Irq12" },
    { ISV34, "Irq34" },
    { ISV56, "Irq56" },
    { ISV78, "Irq78" },
    { DGGExtended, "ExtDGGSettings" },
    { USBSetup, "USBBulkSetup" },
};

QString getRegisterName(u32 registerAddress)
{
    return registerNames.value(registerAddress, QSL("Unknown Register"));
}

QList<u32> getRegisterAddresses()
{
    static const QList<u32> result = registerNames.keys();
    return result;
}

VMEError VMUSB::readAllRegisters(void)
{
    firmwareId = 0;
    globalMode = 0;
    daqSettings = 0;
    ledSources = 0;
    deviceSources = 0;
    dggAsettings = 0;
    dggBsettings = 0;
    scalerAdata = 0;
    scalerBdata = 0;
    eventsPerBuffer = 0;
    irqV[0] = 0;
    irqV[1] = 0;
    irqV[2] = 0;
    irqV[3] = 0;
    extDggSettings = 0;
    usbBulkSetup = 0;

    QVector<QPair<int, u32 *>> regVars =
    {
        { FIDRegister, &firmwareId },
        { GMODERegister, &globalMode },
        { DAQSetRegister, &daqSettings },
        { LEDSrcRegister, &ledSources },
        { DEVSrcRegister, &deviceSources },
        { DGGARegister, &dggAsettings },
        { DGGBRegister, &dggBsettings },
        { ScalerA, &scalerAdata },
        { ScalerB, &scalerBdata },
        { DGGExtended, &extDggSettings },
        { USBSetup, &usbBulkSetup },
        { ISV12, &irqV[0] },
        { ISV34, &irqV[1] },
        { ISV56, &irqV[2] },
        { ISV78, &irqV[3] },
        { EventsPerBuffer, &eventsPerBuffer },
    };

    VMEError error;

    for (auto pair: regVars)
    {
        error = readRegister(pair.first, pair.second);

        if (error.isError())
            return error;
    }

    return error;
}

//
// "get" functions for register field
//

/*!
    \fn vmUsb::getFirmwareId()
 */
u32 VMUSB::getFirmwareId()
{
  return firmwareId;
}

/*!
    \fn vmUsb::getMode()
 */
u32 VMUSB::getMode()
{
  return globalMode;
}

/*!
  \fn vmUsb::getDaqSettings()
 */
u32 VMUSB::getDaqSettings()
{
  return daqSettings;
}
/*!
  \fn vmUsb::getLedSources()
 */
u32 VMUSB::getLedSources()
{
  return ledSources;
}

/*!
  \fn vmUsb::getDeviceSources()
 */
u32 VMUSB::getDeviceSources()
{
  return deviceSources;
}

/*!
  \fn vmUsb::getDggA()
 */
u32 VMUSB::getDggA()
{
  return dggAsettings;
}

/*!
  \fn vmUsb::getDggB()
 */
u32 VMUSB::getDggB()
{
  return dggBsettings;
}

/*!
  \fn vmUsb::getScalerAdata()
 */
u32 VMUSB::getScalerAdata()
{
  return scalerAdata;
}

/*!
  \fn vmUsb::getScalerBdata()
 */
u32 VMUSB::getScalerBdata()
{
  return scalerBdata;
}

/*!
  \fn vmUsb::getNumberMask()
 */
u32 VMUSB::getEventsPerBuffer()
{
  return eventsPerBuffer;
}

/*!
  \fn vmUsb::getDggSettings()
 */
u32 VMUSB::getDggSettings()
{
  return extDggSettings;
}

/*!
  \fn vmUsb::getUsbSettings()
 */
u32 VMUSB::getUsbSettings()
{
  return usbBulkSetup;
}

//
// "set" functions for register field
//
/*!
    \fn vmUsb::setFirmwareId()
 */
int VMUSB::setFirmwareId(int val)
{
    if(!writeRegister(0, val).isError())
    {
        u32 regVal = 0;
        readRegister(0, &regVal);
        firmwareId = regVal;
    }
    return firmwareId;
}

/*!
    \fn vmUsb::setMode()
 */
VMEError VMUSB::setMode(u32 val)
{
    auto result = writeRegister(4, val);

    if (!result.isError())
    {
        u32 regVal;
        result = readRegister(4, &regVal);
        if (!result.isError())
            globalMode = (regVal & 0xFFFF);
    }
    return result;
}

/*!
  \fn vmUsb::setDaqSettings()
 */
VMEError VMUSB::setDaqSettings(u32 val)
{
    VMEError result;

    result = writeRegister(8, val);

    if (!result.isError())
    {
        u32 regVal;
        result = readRegister(8, &regVal);

        if (!result.isError())
            daqSettings = regVal;
    }

    return result;
}

/*!
  \fn vmUsb::setLedSources()
 */
VMEError VMUSB::setLedSources(u32 val)
{
    auto result = writeRegister(LEDSrcRegister, val);

    if (!result.isError())
    {
        u32 regVal;
        result = readRegister(LEDSrcRegister, &regVal);
        if (!result.isError())
            ledSources = regVal;
    }
    return result;
}

/*!
  \fn vmUsb::setDeviceSources()
 */
VMEError VMUSB::setDeviceSources(u32 val)
{
    auto result = writeRegister(DEVSrcRegister, val);

    if (!result.isError())
    {
        u32 regVal;
        result = readRegister(DEVSrcRegister, &regVal);
        if (!result.isError())
            deviceSources = regVal;
    }
    return result;
}

/*!
  \fn vmUsb::setDggA()
 */
int VMUSB::setDggA(int val)
{
    u32 retval;
  if(!writeRegister(20, val).isError()){
    readRegister(20, &retval);
    dggAsettings = retval;
  }
  return dggAsettings;
}

/*!
  \fn vmUsb::setDggB()
 */
int VMUSB::setDggB(int val)
{
    u32 retval;
  if(!writeRegister(24, val).isError()){
    readRegister(24, &retval);
    dggBsettings = retval;
  }
  return dggBsettings;
}

/*!
  \fn vmUsb::setScalerAdata()
 */
int VMUSB::setScalerAdata(int val)
{
    u32 retval;
  if(!writeRegister(28, val).isError()){
    readRegister(28, &retval);
    scalerAdata = retval;
  }
  return scalerAdata;
}

/*!
  \fn vmUsb::setScalerBdata()
 */
int VMUSB::setScalerBdata(int val)
{
    u32 retval;
  if(!writeRegister(32, val).isError()){
    readRegister(32, &retval);
    scalerBdata = retval;
  }
  return scalerBdata;
}

VMEError VMUSB::setEventsPerBuffer(u32 val)
{
    auto result = writeRegister(EventsPerBuffer, val);

    if (!result.isError())
    {
        u32 regVal;
        result = readRegister(EventsPerBuffer, &regVal);

        if (!result.isError())
            eventsPerBuffer = regVal;
    }

    return result;
}

static int irq_vector_register_address(int vec)
{
    switch (vec)
    {
    case 0:
    case 1:
        return ISV12;
    case 2:
    case 3:
        return ISV34;
    case 4:
    case 5:
        return ISV56;
    case 6:
    case 7:
        return ISV78;
    default:
        return -1;
    }
}

/*!
  \fn vmUsb::setIrq()
  Set the zero-based irq service vector vec to the given value val.
 */
VMEError VMUSB::setIrq(int vec, uint16_t val)
{
    int regAddress = irq_vector_register_address(vec);

    if (regAddress < 0)
        return VMEError(QString("setIrq: invalid vector number given (%1)").arg(vec));

    u32 regValue = 0;
    auto error = readRegister(regAddress, &regValue);

    if (error.isError())
        return error;

    val = val & 0xFFFF;

    if (vec % 2 == 0)
    {
        regValue &= 0xFFFF0000;
        regValue |= val;
    }
    else
    {
        regValue &= 0x0000FFFF;
        regValue |= (val << 16);
    }

    error = writeRegister(regAddress, regValue);

    if (error.isError())
        return error;

    error = readRegister(regAddress, &regValue);

    if (error.isError())
        return error;

    int regIndex = vec / 2;
    irqV[regIndex] = regValue;

    return VMEError();
}

/**
  Return the 16-bit interrupt service vector value for the given zero-based vector number. */
uint16_t VMUSB::getIrq(int vec)
{
    int regIndex = vec / 2;

    if (regIndex >= 0 && regIndex < 4)
    {
        int regValue = irqV[regIndex];

        if (vec % 2 == 0)
        {
            return (regValue & 0xFFFF);
        }
        else
        {
            return (regValue >> 16) & 0xFFFF;
        }
    }

    return 0;
}

/**
  Read the 16-bit interrupt service vector value for the given zero-based vector number. */
VMEError VMUSB::readIrq(int vec, u16 *value)
{
    int regAddress = irq_vector_register_address(vec);

    if (regAddress < 0)
        return VMEError(QString("readIrq: invalid vector number given (%1)").arg(vec));

    u32 regValue = 0;
    auto error = readRegister(regAddress, &regValue);

    if (!error.isError())
    {
        int regIndex = vec / 2;
        irqV[regIndex] = regValue;

        if (vec % 2 == 0)
        {
            *value = regValue & 0xFFFF;
        }
        else
        {
            *value = regValue >> 16;
        }
    }

    return error;
}

/*!
  \fn vmUsb::setIrqMask(int val)
 */
VMEError VMUSB::setIrqMask(int val)
{
    uint16_t globalReg;
    uint16_t irqMask = 0x8000 | (val&0x7f);

    error = vmusb->readRegister(GMODERegister, &globalReg);
    if (error.isError())
        return error;

    error = vmusb->writeRegister(GMODERegister, irqMask);
    if (error.isError())
        return error;

    error = vmusb->writeActionRegister(2);
    if (error.isError())
        return error;

    uint32_t maskSet;
    error = vmusb->readRegister(FIDRegister, &maskSet);
    if (error.isError())
        return error;

    irqMask = (maskSet & 0x7f000000) >> 24;
    if (irqMask != val)
        return VMEError(VMEError::BusError, QSL("Read IRQ mask is different from the written!"));

    error = vmusb->writeActionRegister(0);
    if (error.isError())
        return error;

    error = vmusb->setMode(globalReg);
    if (error.isError())
        return error;

    return error;
}

/*!
  \fn vmUsb::resetIrqMask()
 */
VMEError VMUSB::resetIrqMask()
{
    error = setIrqMask(0x7f);

    return error;
}

/*!
  \fn vmUsb::removeIrqMask(int val)
 */
VMEError VMUSB::removeIrqMask(int val)
{
    uint16_t thismask = (~(0x1 << (val - 1)) & 0x7f);
    irqMask &= thismask;

    error = setIrqMask(irqMask);

    return error;
}

/*!
  \fn vmUsb::setDggSettings(int val)
 */
int VMUSB::setDggSettings(int val)
{
    u32 retval;
  if(!writeRegister(56, val).isError()){
    readRegister(56, &retval);
    extDggSettings = retval;
  }
  return extDggSettings;
}

VMEError VMUSB::setUsbSettings(int val)
{
    auto result = writeRegister(USBSetup, val);

    if (!result.isError())
    {
        u32 regVal;
        result = readRegister(USBSetup, &regVal);
        if (!result.isError())
            usbBulkSetup = regVal;
    }
    return result;
}

VMEError VMUSB::writeActionRegister(uint16_t value)
{
    QMutexLocker locker(&m_lock);
    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    u8 outPacket[100];
    u8* pOut = outPacket;

    pOut = static_cast<u8 *>(addToPacket16(pOut, 5)); // Select Register block for transfer.
    pOut = static_cast<u8 *>(addToPacket16(pOut, 10)); // Select action register wthin block.
    pOut = static_cast<u8 *>(addToPacket16(pOut, value));

    // This operation is write only.

    int outSize = pOut - outPacket;

#ifdef WIENER_USE_LIBUSB1
    int transferred = 0;
    int status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_OUT, outPacket, outSize, &transferred, defaultTimeout_ms);

    if (status != 0 || transferred != outSize)
    {
        return VMEError(VMEError::WriteError, status,
                        libusb_strerror(static_cast<libusb_error>(status)),
                        libusb_error_name(status));
    }
#else
    int status = usb_bulk_write(m_deviceHandle, ENDPOINT_OUT, reinterpret_cast<char *>(outPacket), outSize, defaultTimeout_ms);

    if (status != outSize)
    {
        return VMEError(VMEError::WriteError, status, errnoString(-status));
    }
#endif

    bool daqMode = (value & 0x1);

    if (m_daqMode != daqMode)
    {
        m_daqMode = daqMode;
        if (daqMode)
        {
            //emit daqModeEntered();
        }
        else
        {
            //emit daqModeLeft();
        }
        //emit daqModeChanged(daqMode);
    }

    return VMEError();
}

/*!
    \fn vmUsb::setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay)
 */
int VMUSB::setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay)
{
    // redundant function to setDaqSettings - allows separate setting of all three components
  u32 val = 0x10000 * frequency + 0x100 * period + delay;
    u32 retval = {};

  if(!writeRegister(8, val).isError()){
    readRegister(8, &retval);
    daqSettings = retval;
  }
  else
    qDebug("Write failed");
  qDebug("timing val = %x, register: %x", val, retval);
  return daqSettings;
}

//  Utility to create a stack from a transfer address word and
//  a CVMUSBReadoutList and an optional list offset (for non VCG lists).
//  Parameters:
//     uint16_t ta               The transfer address word.
//     CVMUSBReadoutList& list:  The list of operations to create a stack from.
//     size_t* outSize:          Pointer to be filled in with the final out packet size
//     off_t   offset:           If VCG bit is clear and VCS is set, the bottom
//                               16 bits of this are put in as the stack load
//                               offset. Otherwise, this is ignored and
//                               the list lize is treated as a 32 bit value.
//  Returns:
//     A uint16_t* for the list. The result is dynamically allocated
//     and must be released via delete []p e.g.
//
uint16_t*
listToOutPacket(uint16_t ta, CVMUSBReadoutList* list,
                        size_t* outSize, off_t offset)
{
    int listLongwords = list->size();
    int listShorts    = listLongwords*sizeof(uint32_t)/sizeof(uint16_t);
    int packetShorts    = (listShorts + 3);
    uint16_t* outPacket = new uint16_t[packetShorts];
    uint16_t* p         = outPacket;

    // Fill the outpacket:

    p = static_cast<uint16_t*>(addToPacket16(p, ta));
    //
    // The next two words depend on which bits are set in the ta
    //
    if(ta & TAVcsIMMED) {
      p = static_cast<uint16_t*>(addToPacket32(p, listShorts+1)); // 32 bit size.
    }
    else {
      p = static_cast<uint16_t*>(addToPacket16(p, listShorts+1)); // 16 bits only.
      p = static_cast<uint16_t*>(addToPacket16(p, offset));       // list load offset.
    }

    std::vector<uint32_t> stack = list->get();
    for (int i = 0; i < listLongwords; i++) {
        p = static_cast<uint16_t*>(addToPacket32(p, stack[i]));
    }
    *outSize = packetShorts*sizeof(uint16_t);
    return outPacket;
}

VMEError VMUSB::listExecute(CVMUSBReadoutList *list, void *readBuffer, size_t readBufferSize, size_t *bytesRead)
{
  size_t outSize;
  uint16_t* outPacket = listToOutPacket(TAVcsWrite | TAVcsIMMED, list, &outSize);

  auto vmeError = transaction(outPacket, outSize, readBuffer, readBufferSize, bytesRead);

  delete []outPacket;

  return vmeError;
}

VMEError VMUSB::listLoad(CVMUSBReadoutList *list, uint8_t stackID, size_t stackMemoryOffset, int timeout_ms)
{
    QMutexLocker locker(&m_lock);
    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    // Note (flueke): taken from NSCLDAQ
    // Need to construct the TA field, straightforward except for the list number
    // which is splattered all over creation.
    uint16_t ta = TAVcsSel | TAVcsWrite;
    if (stackID & 1)  ta |= TAVcsID0;
    if (stackID & 2)  ta |= TAVcsID1; // Probably the simplest way for this
    if (stackID & 4)  ta |= TAVcsID2; // few bits.

    size_t   packetSize;
    uint16_t *outPacket = listToOutPacket(ta, list, &packetSize, stackMemoryOffset);

#ifdef WIENER_USE_LIBUSB1
    int transferred = 0;
    int status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_OUT,
                                      reinterpret_cast<u8 *>(outPacket),
                                      packetSize, &transferred, timeout_ms);
#else
    int status = usb_bulk_write(m_deviceHandle, ENDPOINT_OUT,
                                reinterpret_cast<char *>(outPacket),
                                packetSize, timeout_ms);
#endif

    delete []outPacket;

#ifdef WIENER_USE_LIBUSB1
    if (status != 0)
    {
        return VMEError(VMEError::WriteError, status,
                        libusb_strerror(static_cast<libusb_error>(status)),
                        libusb_error_name(status));
    }
#else
    if (status < 0)
    {
        return VMEError(VMEError::WriteError, status, errnoString(-status));
    }
#endif

    return VMEError();
}

/*
   Utility function to perform a 'symmetric' transaction.
   Most operations on the VM-USB are 'symmetric' USB operations.
   This means that a usb_bulk_write will be done followed by a
   usb_bulk_read to return the results/status of the operation requested
   by the write.
   Parameters:
   void*   writePacket   - Pointer to the packet to write.
   size_t  writeSize     - Number of bytes to write from writePacket.

   void*   readPacket    - Pointer to storage for the read.
   size_t  readSize      - Number of bytes to attempt to read.

   size_t* bytesRead     - The number of bytes read is stored here.


   Returns: VMEError structure filled with error info if an error occured.
*/
VMEError
VMUSB::transaction(void* writePacket, size_t writeSize,
        void* readPacket,  size_t readSize, size_t *bytesRead, int timeout_ms)
{
    QMutexLocker locker(&m_lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

#ifdef WIENER_USE_LIBUSB1
    int transferred = 0;
    int status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_OUT,
                                      reinterpret_cast<u8 *>(writePacket),
                                      writeSize, &transferred, timeout_ms);

    if (status != 0)
    {
        return VMEError(VMEError::WriteError, status,
                        libusb_strerror(static_cast<libusb_error>(status)),
                        libusb_error_name(status));
    }

    transferred = 0;
    status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_IN,
                                      reinterpret_cast<u8 *>(readPacket),
                                      readSize, &transferred, timeout_ms);

    if (status != 0)
    {
        if (status != LIBUSB_ERROR_TIMEOUT)
        {
            return VMEError(VMEError::ReadError, status,
                            libusb_strerror(static_cast<libusb_error>(status)),
                            libusb_error_name(status));
        }
        else
        {
            return VMEError(VMEError::Timeout, status,
                            libusb_strerror(static_cast<libusb_error>(status)),
                            libusb_error_name(status));
        }
    }

    *bytesRead = transferred;
#else
    int status = usb_bulk_write(m_deviceHandle, ENDPOINT_OUT,
                                static_cast<char*>(writePacket), writeSize,
                                timeout_ms);

    if (status < 0)
    {
        return VMEError(VMEError::WriteError, status, errnoString(-status));
    }

    status = usb_bulk_read(m_deviceHandle, ENDPOINT_IN,
                           static_cast<char*>(readPacket), readSize, timeout_ms);

    if (status < 0)
    {
        return VMEError(VMEError::ReadError, status, errnoString(-status));
    }

    *bytesRead = status;
#endif

    return {};
}

VMEError VMUSB::bulkRead(void *destBuffer, size_t outBufferSize, int *transferred, int timeout_ms)
{
    Q_ASSERT(transferred);
    QMutexLocker locker(&m_lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

#ifdef WIENER_USE_LIBUSB1
    int status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_IN,
                                      reinterpret_cast<u8 *>(destBuffer),
                                      outBufferSize, transferred, timeout_ms);

    if (status != 0)
    {
        if (status != LIBUSB_ERROR_TIMEOUT)
        {
            return VMEError(VMEError::ReadError, status,
                            libusb_strerror(static_cast<libusb_error>(status)),
                            libusb_error_name(status));
        }
        else
        {
            return VMEError(VMEError::Timeout, status,
                            libusb_strerror(static_cast<libusb_error>(status)),
                            libusb_error_name(status));
        }
    }
#else
    int status = usb_bulk_read(m_deviceHandle, ENDPOINT_IN,
                               static_cast<char *>(destBuffer), outBufferSize, timeout_ms);

    if (status < 0)
    {
        return VMEError(VMEError::ReadError, status, errnoString(-status));
    }

    *transferred = status;
#endif

    return VMEError();
}

VMEError VMUSB::bulkWrite(void *sourceBuffer, size_t sourceBufferSize, int *transferred, int timeout_ms)
{
    Q_ASSERT(transferred);
    QMutexLocker locker(&m_lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

#ifdef WIENER_USE_LIBUSB1
    int status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_OUT,
                                      reinterpret_cast<u8 *>(sourceBuffer),
                                      sourceBufferSize, transferred, timeout_ms);

    if (status != 0)
    {
        if (status != LIBUSB_ERROR_TIMEOUT)
        {
            return VMEError(VMEError::WriteError, status,
                            libusb_strerror(static_cast<libusb_error>(status)),
                            libusb_error_name(status));
        }
        else
        {
            return VMEError(VMEError::Timeout, status,
                            libusb_strerror(static_cast<libusb_error>(status)),
                            libusb_error_name(status));
        }
    }
#else
    int status = usb_bulk_write(m_deviceHandle, ENDPOINT_OUT,
                                static_cast<char *>(sourceBuffer), sourceBufferSize, timeout_ms);

    if (status < 0)
    {
        return VMEError(VMEError::WriteError, status, errnoString(-status));
    }

    *transferred = status;
#endif
    return VMEError();
}

VMEError
VMUSB::stackWrite(u8 stackNumber, u32 loadOffset, const QVector<u32> &stackData)
{
    CVMUSBReadoutList stackList(stackData);
    return listLoad(&stackList, stackNumber, loadOffset);
}

static const s32 StackReadTimeout_ms = 100;

VMEError VMUSB::stackRead(u8 stackID, QVector<u32> &stackOut, u32 &loadOffsetOut)
{
    u16 ta = TAVcsSel;
    if (stackID & 1)  ta |= TAVcsID0;
    if (stackID & 2)  ta |= TAVcsID1; // Probably the simplest way for this
    if (stackID & 4)  ta |= TAVcsID2; // few bits.

    QMutexLocker locker(&m_lock);

    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    int transferred = 0;
    u16 inBuffer[2048] = {};

#ifdef WIENER_USE_LIBUSB1
    int status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_OUT,
                                      reinterpret_cast<u8 *>(&ta),
                                      sizeof(ta), &transferred, StackReadTimeout_ms);

    if (status != 0)
    {
        return VMEError(VMEError::WriteError, status,
                        libusb_strerror(static_cast<libusb_error>(status)),
                        libusb_error_name(status));
    }

    transferred = 0;
    status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_IN,
                                      reinterpret_cast<u8 *>(&inBuffer),
                                      sizeof(inBuffer), &transferred, StackReadTimeout_ms);

    if (status != 0 && status != LIBUSB_ERROR_TIMEOUT)
    {
        return VMEError(VMEError::ReadError, status,
                        libusb_strerror(static_cast<libusb_error>(status)),
                        libusb_error_name(status));
    }
#else
    int status = usb_bulk_write(m_deviceHandle, ENDPOINT_OUT, reinterpret_cast<char *>(&ta), sizeof(ta), 100);

    if (status < 0)
        return VMEError(VMEError::WriteError, status, errnoString(-status));

    transferred = usb_bulk_read(m_deviceHandle, ENDPOINT_IN, reinterpret_cast<char *>(&inBuffer), sizeof(inBuffer), 100);

    if (transferred <= 0)
        return VMEError(VMEError::ReadError, transferred, errnoString(-transferred));
#endif

    qDebug("stackRead begin");
    for (size_t i=0; i<transferred/sizeof(u16); ++i)
    {
        qDebug("  %04x", inBuffer[i]);
    }
    qDebug("stackRead end");

    loadOffsetOut = inBuffer[1];

    u32 *bufp = reinterpret_cast<u32 *>(inBuffer + 2);
    u32 *endp = reinterpret_cast<u32 *>(inBuffer + transferred/sizeof(u16));

    while (bufp < endp)
        stackOut.append(*bufp++);

    return VMEError();
}

VMEError
VMUSB::stackExecute(const QVector<u32> &stackData, size_t resultMaxWords, QVector<u32> *dest)
{
    dest->resize(resultMaxWords);
    CVMUSBReadoutList stackList(stackData);
    size_t bytesRead = 0;
    auto error = listExecute(&stackList, dest->data(), dest->size() * sizeof(u32), &bytesRead);
    dest->resize(bytesRead / sizeof(u32));
    return error;
}

VMEError VMUSB::write32(u32 address, u32 value, u8 amod)
{
    CVMUSBReadoutList readoutList;
    readoutList.addWrite32(address, amod, value);

    /* Always use a 32-bit value to hold the response not taking into account
     * the current alignment mode. In 16-bit alignment mode the VMUSB will only
     * yield a 16-bit word but that does not cause any problems here. If not
     * using a 32-bit value we'd need to handle the align16 and align32 cases
     * separately. */
    u32 response = 0;
    size_t bytesRead = 0;
    auto error = listExecute(&readoutList, &response, sizeof(response), &bytesRead);

    if (error.isError() && !error.isTimeout())
        return error;

    if (response == 0)
        return VMEError(VMEError::BusError, QSL("No DTACK on write"));

    return {};
}

VMEError VMUSB::write16(u32 address, u16 value, u8 amod)
{
    CVMUSBReadoutList readoutList;
    readoutList.addWrite16(address, amod, value);

    /* Same as in write32: always use a 32-bit value to hold the result. */
    u32 response = 0;
    size_t bytesRead = 0;
    auto error = listExecute(&readoutList, &response, sizeof(response), &bytesRead);

    if (error.isError() && !error.isTimeout())
        return error;

    if (response == 0)
        return VMEError(VMEError::BusError, QSL("No DTACK on write"));

    return {};
}

VMEError VMUSB::read32(u32 address, u32 *value, u8 amod)
{
    size_t bytesRead = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRead32(address, amod);
    return listExecute(&readoutList, value, sizeof(*value), &bytesRead);

}

VMEError VMUSB::read16(u32 address, u16 *value, u8 amod)
{
    size_t bytesRead = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRead16(address, amod);
    return listExecute(&readoutList, value, sizeof(*value), &bytesRead);

}

VMEError VMUSB::blockRead(u32 address, u32 transfers, QVector<u32> *dest, u8 amod, bool fifo)
{
    CVMUSBReadoutList readoutList;

    if (fifo)
        readoutList.addFifoRead32(address, amod, transfers);
    else
        readoutList.addBlockRead32(address, amod, transfers);

    bool isMblt = (amod == CVMUSBReadoutList::a32UserBlock64
                   || amod == CVMUSBReadoutList::a32PrivBlock64);

    dest->resize(isMblt ? transfers * 2 : transfers);

    size_t bytesRead = 0;
    auto result = listExecute(&readoutList, dest->data(), dest->size() * sizeof(u32), &bytesRead);
    dest->resize(bytesRead / sizeof(u32));

    return result;
}

VMEError VMUSB::enterDaqMode(u32 additionalBits)
{
    additionalBits |= 1u;
    return writeActionRegister(additionalBits);
}

VMEError VMUSB::leaveDaqMode(u32 additionalBits)
{
    additionalBits &= ~1u; // unset DAQ mode bit
    return writeActionRegister(additionalBits);
}

static const s32 ErrorRecoveryReadTimeout_ms = 250;

VMEError VMUSB::tryErrorRecovery()
{
    if (!isOpen())
        return VMEError(VMEError::NotOpen);

    QMutexLocker locker(&m_lock);

    int bytesRead = 0;
    do
    {
        u8 buffer[VMUSBBufferSize];

#ifdef WIENER_USE_LIBUSB1
        int status = libusb_bulk_transfer(m_deviceHandle, ENDPOINT_IN, buffer, sizeof(buffer), &bytesRead, ErrorRecoveryReadTimeout_ms);
        qDebug() << __PRETTY_FUNCTION__ << libusb_strerror(static_cast<libusb_error>(status)) << bytesRead;
#else
        bytesRead = usb_bulk_read(m_deviceHandle, ENDPOINT_IN, reinterpret_cast<char *>(buffer), sizeof(buffer), 250);
        qDebug("bulk read returned %d", bytesRead);
#endif
    } while (bytesRead > 0);

    return leaveDaqMode();
}

void dump_registers(VMUSB *vmusb, Dumper dumper)
{
    dumper(QSL("Begin VMUSB register dump"));
    for (u32 addr: getRegisterAddresses())
    {
        u32 value = 0;
        auto error = vmusb->readRegister(addr, &value);
        auto name = getRegisterName(addr);
        if (error.isError())
        {
            dumper(QString("  Error reading register (%1, 0x%2, %3)")
                   .arg(addr)
                   .arg(addr, 2, 16, QLatin1Char('0'))
                   .arg(name));
        }
        else
        {
            dumper(QString("  0x%2, %1 = 0x%3 (%4)")
                   .arg(addr, 2, 10, QLatin1Char(' '))
                   .arg(addr, 2, 16, QLatin1Char('0'))
                   .arg(value, 8, 16, QLatin1Char('0'))
                   .arg(name));
        }
    }
    dumper(QSL("End VMUSB register dump"));
}
