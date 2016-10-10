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


VMUSB::VMUSB()
    : m_state(ControllerState::Closed)
    , m_lock(QMutex::Recursive)
{
}


VMUSB::~VMUSB()
{
    closeUsbDevice();
}

bool VMUSB::openFirstUsbDevice(void)
{
    QMutexLocker locker(&m_lock);
    if (isOpen())
    {
        return false;
    }

    getUsbDevices();

    if (numDevices <= 0)
    {
        return false;
    }

    hUsbDevice = xxusb_device_open(pUsbDevice[0].usbdev);

    if (hUsbDevice)
    {
        m_currentSerialNumber = pUsbDevice[0].SerialString;
        // clear the action register (makes sure daq mode is disabled)
        if (tryErrorRecovery())
        {
            m_state = ControllerState::Opened;
            emit controllerOpened();
            emit controllerStateChanged(m_state);
        }
        else
        {
            closeUsbDevice();
        }
    }

    return hUsbDevice;
}

void VMUSB::closeUsbDevice(void)
{
    QMutexLocker locker (&m_lock);
    if (hUsbDevice)
    {
        xxusb_device_close(hUsbDevice);
        hUsbDevice = nullptr;
        m_currentSerialNumber = QString();
        m_state = ControllerState::Closed;
        emit controllerClosed();
        emit controllerStateChanged(m_state);
    }
}

struct VMUSB_Firmware
{
    VMUSB_Firmware(uint32_t v)
    {
        month = (v >> 29) & 0x7;
        year  = (v >> 24) & 0x1f;
        device_id = (v >> 20) & 0xf;
        beta_version = (v >> 16) & 0xf;
        major_revision = (v >> 8) & 0xff;
        minor_revision = v & 0xff;
    }

    uint8_t month;
    uint8_t year;
    uint8_t device_id;
    uint8_t beta_version;
    uint8_t major_revision;
    uint8_t minor_revision;
};

bool VMUSB::readRegister(u32 address, u32 *outValue)
{
    if (isOpen())
    {
        QMutexLocker locker(&m_lock);
        long a = address;
        long o = 0;
        int status = VME_register_read(hUsbDevice, a, &o);

        if (status >= 0)
        {
            *outValue = o;
            return true;
        }
        else
        {
            lastUsbError = status;
        }
    }

    return false;
}

bool VMUSB::writeRegister(u32 address, u32 value)
{
    if (isOpen())
    {
        QMutexLocker locker(&m_lock);
        long a = address;
        long v = value;
        int status = VME_register_write(hUsbDevice, a, v);

        if (status < 0)
        {
            lastUsbError = status;
            return false;
        }
    }

    return true;
}

void VMUSB::readAllRegisters(void)
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

  u32 val = 0;
  int status = 0;

  QMutexLocker locker(&m_lock);

  status = readRegister(0, &val);
  if (status)
    firmwareId = (int) val;
  qDebug("Id: %x",firmwareId);


  status = readRegister(4, &val);
  if (status)
    globalMode = (int)val & 0xFFFF;

  qDebug("globalMode: %x", globalMode);

  status = readRegister(8, &val);
  if (status)
    daqSettings = (int)val;

  status = readRegister(12, &val);
  if (status)
    ledSources = (int)val;

  status = readRegister(16, &val);
  if (status)
    deviceSources = (int)val;

  status = readRegister(20, &val);
  if (status)
    dggAsettings = (int)val;

  status = readRegister(24, &val);
  if (status)
    dggBsettings = (int)val;

  status = readRegister(28, &val);
  if (status)
    scalerAdata = (int)val;

  status = readRegister(32, &val);
  if (status)
    scalerBdata = (int)val;

  status = readRegister(36, &val);
  if (status)
    eventsPerBuffer = (u32)val;

  status = readRegister(40, &val);
  if (status)
    irqV[0] = (int)val;

  status = readRegister(44, &val);
  if (status)
    irqV[1] = (int)val;

  status = readRegister(48, &val);
  if (status)
    irqV[2] = (int)val;

  status = readRegister(52, &val);
  if (status)
    irqV[3] = (int)val;

  status = readRegister(56, &val);
  if (status)
    extDggSettings = (int)val;

  status = readRegister(60, &val);
  if (status)
    usbBulkSetup = (int)val;
}




/*!
    \fn vmUsb::getUsbDevices(void)
 */

 void VMUSB::getUsbDevices(void)
 {
     QMutexLocker locker(&m_lock);

     short DevFound = 0;
     usb_dev_handle *udev;
     struct usb_bus *bus;
     struct usb_device *dev;
     struct usb_bus *usb_busses_;
     char string[256];
     short ret;

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
                qDebug("found wiener device");
                udev = usb_open(dev);
                //qDebug("result of open: %d", udev);
                if (udev)
                {
                    qDebug("opened wiener device");

                    if (usb_get_string_simple(udev, dev->descriptor.iSerialNumber, pUsbDevice[DevFound].SerialString,
                            sizeof(pUsbDevice[DevFound].SerialString)) < 0)
                    {
                        pUsbDevice[DevFound].SerialString[0] = '\0';
                    }

                    pUsbDevice[DevFound].usbdev=dev;
                    //    strcpy(xxdev[DevFound].SerialString, string);
                    DevFound++;
                    usb_close(udev);
                }
            }
        }
     }
    numDevices = DevFound;
}

//
// "get" functions for register field
//

/*!
    \fn vmUsb::getFirmwareId()
 */
int VMUSB::getFirmwareId()
{
  return firmwareId;
}

/*!
    \fn vmUsb::getMode()
 */
int VMUSB::getMode()
{
  return globalMode;
}

/*!
  \fn vmUsb::getDaqSettings()
 */
int VMUSB::getDaqSettings()
{
  return daqSettings;
}
/*!
  \fn vmUsb::getLedSources()
 */
int VMUSB::getLedSources()
{
  return ledSources;
}

/*!
  \fn vmUsb::getDeviceSources()
 */
int VMUSB::getDeviceSources()
{
  return deviceSources;
}

/*!
  \fn vmUsb::getDggA()
 */
int VMUSB::getDggA()
{
  return dggAsettings;
}

/*!
  \fn vmUsb::getDggB()
 */
int VMUSB::getDggB()
{
  return dggBsettings;
}

/*!
  \fn vmUsb::getScalerAdata()
 */
int VMUSB::getScalerAdata()
{
  return scalerAdata;
}

/*!
  \fn vmUsb::getScalerBdata()
 */
int VMUSB::getScalerBdata()
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
int VMUSB::getDggSettings()
{
  return extDggSettings;
}

/*!
  \fn vmUsb::getUsbSettings()
 */
int VMUSB::getUsbSettings()
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
    u32 retval;
  if(writeRegister(0, val)){
    readRegister(0, &retval);
    firmwareId = (int)retval;
  }
  return firmwareId;
}

/*!
    \fn vmUsb::setMode()
 */
int VMUSB::setMode(int val)
{
    u32 retval;
  if(writeRegister(4, val)){
    readRegister(4, &retval);
    globalMode = (int)retval & 0xFFFF;
  }
  return globalMode;
}

/*!
  \fn vmUsb::setDaqSettings()
 */
int VMUSB::setDaqSettings(int val)
{
    u32 retval;
  if(writeRegister(8, val)){
    readRegister(8, &retval);
    daqSettings = (int)retval;
  }
  return daqSettings;
}
/*!
  \fn vmUsb::setLedSources()
 */
int VMUSB::setLedSources(int val)
{
    u32 retval;
  if(writeRegister(12, val)){
    readRegister(12, &retval);
    ledSources = (int)retval;
  }
  qDebug("set value: %x", ledSources);
  return ledSources;
}

/*!
  \fn vmUsb::setDeviceSources()
 */
int VMUSB::setDeviceSources(int val)
{
    u32 retval;
  if(writeRegister(16, val)){
    readRegister(16, &retval);
    deviceSources = (int)retval;
  }
  return deviceSources;
}

/*!
  \fn vmUsb::setDggA()
 */
int VMUSB::setDggA(int val)
{
    u32 retval;
  if(writeRegister(20, val)){
    readRegister(20, &retval);
    dggAsettings = (int)retval;
  }
  return dggAsettings;
}

/*!
  \fn vmUsb::setDggB()
 */
int VMUSB::setDggB(int val)
{
    u32 retval;
  if(writeRegister(24, val)){
    readRegister(24, &retval);
    dggBsettings = (int)retval;
  }
  return dggBsettings;
}

/*!
  \fn vmUsb::setScalerAdata()
 */
int VMUSB::setScalerAdata(int val)
{
    u32 retval;
  if(writeRegister(28, val)){
    readRegister(28, &retval);
    scalerAdata = (int)retval;
  }
  return scalerAdata;
}

/*!
  \fn vmUsb::setScalerBdata()
 */
int VMUSB::setScalerBdata(int val)
{
    u32 retval;
  if(writeRegister(32, val)){
    readRegister(32, &retval);
    scalerBdata = (int)retval;
  }
  return scalerBdata;
}

u32 VMUSB::setEventsPerBuffer(u32 val)
{
    u32 retval;
  if(writeRegister(36, val)){
    readRegister(36, &retval);
    eventsPerBuffer = (u32)retval;
  }
  return eventsPerBuffer;
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
int VMUSB::setIrq(int vec, uint16_t val)
{
    int regAddress = irq_vector_register_address(vec);

    if (regAddress < 0)
        throw std::runtime_error("setIrq: invalid vector number given");

    u32 regValue = 0;

    if (regAddress >= 0 && readRegister(regAddress, &regValue))
    {
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

        if (writeRegister(regAddress, regValue) &&
                readRegister(regAddress, &regValue))
        {
            int regIndex = vec / 2;
            irqV[regIndex] = regValue;
            return regValue;
        }
    }

    return -1;
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

/*!
  \fn vmUsb::setDggSettings(int val)
 */
int VMUSB::setDggSettings(int val)
{
    u32 retval;
  if(writeRegister(56, val) > 0){
    readRegister(56, &retval);
    extDggSettings = (int)retval;
  }
  return extDggSettings;
}

/*!
  \fn vmUsb::setUsbSettings(int val)
 */
int VMUSB::setUsbSettings(int val)
{
    u32 retval;
  if(writeRegister(60, val) > 0){
    readRegister(60, &retval);
    usbBulkSetup = (int)retval;
  }
  return usbBulkSetup;
}


short VMUSB::vmeWrite16(long addr, long data)
{
    return vmeWrite16(addr, data, VME_AM_A32_USER_PROG);
}

/*!
    \fn vmUsb::vmeWrite16(short am, long addr, long data)
 */
short VMUSB::vmeWrite16(long addr, long data, uint8_t amod)
{
  long intbuf[1000];
  short ret;
//	qDebug("Write16");
  data &= 0xFFFF;
//	swap16(&data);
  intbuf[0]=7;
  intbuf[1]=0;
  intbuf[2]=amod;
  intbuf[3]=0x0; //  0x1; // little endianess -> PC
  if(bigendian)
    intbuf[3] |= 0x1;
  intbuf[4]=(addr & 0xffff) | 0x0001; // 16 bit Zugriff
  intbuf[5]=((addr >>16) & 0xffff);
  intbuf[6]=(data & 0xffff);
  intbuf[7]=((data >> 16) & 0xffff);
//	for(int i=0;i<8;i++)
//		qDebug("%x", intbuf[i]);
  QMutexLocker locker(&m_lock);
  if (!isOpen())
  {
      return -3;
  }
  ret = xxusb_stack_execute(hUsbDevice, intbuf);
#if 0
  for (int i=0; i<ret; ++i)
  {
    qDebug("vmeWrite16: %x", ((uchar *)intbuf)[i]);
  }
#endif
  return ret;
}

/*!
    \fn vmUsb::vmeWrite32(short am, long addr, long data)
 */

short VMUSB::vmeWrite32(long addr, long data)
{
    return vmeWrite32(addr, data, VME_AM_A32_USER_PROG);
}

short VMUSB::vmeWrite32(long addr, long data, uint8_t amod)
{
  long intbuf[1000];
  short ret;
//    qDebug("Write32 %lx %lx", addr, data);
//	swap32(&data);
  intbuf[0]=7;
  intbuf[1]=0;
  intbuf[2]=amod;
  intbuf[3]=0; // | 0x1; // little endianess -> PC
  if(bigendian)
    intbuf[3] |= 0x1;
  intbuf[4]=(addr & 0xffff); // | 0x0001; // 16 bit Zugriff
  intbuf[5]=((addr >> 16) & 0xffff);
  intbuf[6]=(data & 0xffff);
  intbuf[7]=((data >> 16) & 0xffff);
//    for(int i=0;i<8;i++)
//        qDebug("%x", intbuf[i]);
  QMutexLocker locker(&m_lock);
  if (!isOpen())
  {
      return -3;
  }
  ret = xxusb_stack_execute(hUsbDevice, intbuf);
  return ret;
}

/*!
    \fn vmUsb::vmeRead32(long addr, long* data)
 */
short VMUSB::vmeRead32(long addr, long* data)
{
  long intbuf[1000];
  short ret;
//	qDebug("Read32");
  intbuf[0]=5;
  intbuf[1]=0;
  intbuf[2]= VME_AM_A32_USER_PROG | 0x100;
  intbuf[3]= 0; // | 0x1; // little endianess -> PC
  if(bigendian)
    intbuf[3] |= 0x1;
  intbuf[4]=(addr & 0xffff); // | 0x0001; // 16 bit Zugriff
  intbuf[5]=((addr >> 16) & 0xffff);
//	for(int i=0;i<6;i++)
//		qDebug("%lx", intbuf[i]);
  QMutexLocker locker(&m_lock);
  if (!isOpen())
  {
      return -3;
  }
  ret = xxusb_stack_execute(hUsbDevice, intbuf);
//	qDebug("read: %d %lx %lx", ret, intbuf[0], intbuf[1]);
  *data = intbuf[0] + (intbuf[1] * 0x10000);
//	swap32(data);
  return ret;
}

/*!
    \fn vmUsb::vmeRead16(short am, long addr, long* data)
 */
short VMUSB::vmeRead16(long addr, long* data)
{
  long intbuf[1000];
  short ret;
//	qDebug("Read16");
  intbuf[0]=5;
  intbuf[1]=0;
  intbuf[2]= VME_AM_A32_USER_PROG | 0x100;
  intbuf[3] = 0;
  if(bigendian)
    intbuf[3]= 1; // | 0x1; // endianess
  intbuf[4]=(addr & 0xffff) | 0x0001; // 16 bit Zugriff
  intbuf[5]=((addr >>16) & 0xffff);
  QMutexLocker locker(&m_lock);
  if (!isOpen())
  {
      return -3;
  }
  ret = xxusb_stack_execute(hUsbDevice, intbuf);
//	qDebug("read: %lx %lx", intbuf[0], intbuf[1]);
  *data = intbuf[0] + (intbuf[1] * 0x10000);
//	swap16(data);
  *data &= 0xFFFF;
  return ret;
}

/*!
    \fn vmUsb::vmeBltRead32(short am, long addr, ushort count, long* data)
 */
int VMUSB::vmeBltRead32(long addr, int count, quint32* data)
{
#if 0
  long intbuf[1000];
  short ret;
  int i=0;
  qDebug("vmUsb::ReadBlt32: addr=0x%08lx, count=%d", addr, count);
  if (count >= 255) return -1;
  intbuf[0]=5;
  intbuf[1]=0;
  intbuf[2]=VME_AM_A32_PRIV_BLT | 0x100;
  intbuf[3]=(count << 8);
  if(bigendian)
    intbuf[3] |= 0x1; // little endianess -> PC
  intbuf[4]=(addr & 0xffff);
  intbuf[5]=((addr >>16) & 0xffff);

  qDebug("vmUsb::ReadBlt32: stack:");
    for(int i=0;i<intbuf[0];i++)
    qDebug("  0x%08lx", intbuf[i]);


  ret = xxusb_stack_execute(hUsbDevice, intbuf);
  int j=0;
  for (i=0;i<(2*count);i=i+2)
  {
    data[j]=intbuf[i] + (intbuf[i+1] * 0x10000);
//		swap32(&data[j]);
    j++;
  }
  return ret;
#else
    CVMUSBReadoutList readoutList;

    readoutList.addBlockRead32(addr, VME_AM_A32_USER_BLT, count);

    size_t bytesRead = 0;

    int status = listExecute(&readoutList, data, count * sizeof(quint32), &bytesRead);

    if (status < 0)
        return status;

    return bytesRead;
#endif
}

int VMUSB::vmeMbltRead32(long addr, int count, quint32 *data)
{
#if 0
  long intbuf[1000];
  short ret;
  int i=0;
  qDebug("vmUsb::ReadMblt32: addr=0x%08lx, count=%d", addr, count);
  if (count > 256) return -1;

  intbuf[0]=5;
  intbuf[1]=0;
  intbuf[2]=VME_AM_A32_USER_MBLT | 0x100;
  intbuf[3]=(count << 8);
  if(bigendian)
    intbuf[3] |= 0x1; // little endianess -> PC
  intbuf[4]=(addr & 0xffff);
  intbuf[5]=((addr >>16) & 0xffff);


  qDebug("vmUsb::ReadMblt32: stack:");
    for(int i=0;i<intbuf[0];i++)
    qDebug("  0x%08lx", intbuf[i]);

  ret = xxusb_stack_execute(hUsbDevice, intbuf);
  int j=0;
  for (i=0;i<(2*count);i=i+2)
  {
    data[j]=intbuf[i] + (intbuf[i+1] * 0x10000);
//		swap32(&data[j]);
    j++;
  }
  return ret;
#else
    CVMUSBReadoutList readoutList;

    readoutList.addBlockRead32(addr, VME_AM_A32_USER_MBLT, count);

    readoutList.dump(std::cout);

    size_t bytesRead = 0;

    int status = listExecute(&readoutList, data, count * sizeof(quint64), &bytesRead);

    if (status < 0)
        return status;

    return bytesRead;
#endif
}

/*!
    \fn vmUsb::swap32(long val)
 */
void VMUSB::swap32(long* val)
{
  unsigned char * dat;
  unsigned char mem;
  dat = (unsigned char *) val;

//	qDebug("raw: %lx: %x %x %x %x", *val, dat[3], dat[2], dat[1], dat[0]);

  mem = dat[3];
  dat[3] = dat[0];
  dat[0] = mem;
  mem = dat[2];
  dat[2] = dat[1];
  dat[1] = mem;

//	qDebug("swapped: %lx: %x %x %x %x", *val, dat[3], dat[2], dat[1], dat[0]);
}

/*!
    \fn vmUsb::swap16(long val)
 */
void VMUSB::swap16(long* val)
{
  unsigned char * dat;
  unsigned char mem;
  dat = (unsigned char *) val;

  qDebug("raw: %lx: %x %x", *val, dat[1], dat[0]);

  mem = dat[1];
  dat[1] = dat[0];
  dat[0] = mem;

  qDebug("swapped: %lx: %x %x", *val, dat[1], dat[0]);
}




/*!
    \fn vmUsb::stackWrite(int id, long* data)
 */
int VMUSB::stackWrite(int id, long* data)
{
  qDebug("StackWrite: id=%d, stackSize=%d", id, data[0]);

  for(int i=0;i<=data[0];i++)
    qDebug("  %d: %lx", i, data[i]);

  unsigned char addr[8] = {2, 3, 18, 19, 34, 35, 50, 51};
  QMutexLocker locker(&m_lock);
  if (!isOpen())
  {
      return -3;
  }
  int ret = xxusb_stack_write(hUsbDevice, addr[id], data);
  qDebug("StackWrite %d (addr: %d), %d bytes", id, addr[id], ret);
  return ret;
}


/*!
    \fn vmUsb::stackRead(long* data)
 */
int VMUSB::stackRead(int id, long* data)
{
  unsigned char addr[8] = {2, 3, 18, 19, 34, 35, 50, 51};
  qDebug("StackRead %d (addr: %d)", id, addr[id]);

  QMutexLocker locker(&m_lock);
  if (!isOpen())
  {
      return -3;
  }
  int ret = xxusb_stack_read(hUsbDevice, addr[id], data);

  return ret;
}


/*!
    \fn vmUsb::stackExecute(long* data)
 */
int VMUSB:: stackExecute(long* data)
{
    int ret, i, j;
    qDebug("StackExecute: stackSize=%d", data[0]);

    for(i=0;i<=data[0];i++)
        qDebug("  %d: %08lx", i, data[i]);

    QMutexLocker locker(&m_lock);
    if (!isOpen())
    {
        return -3;
    }
    ret = xxusb_stack_execute(hUsbDevice, data);

    qDebug("read: %d %lx %lx", ret, data[0], data[1]);

    qDebug("retrieved:");
    for (i=0, j=0;i<ret/2;i=i+2)
    {
        data[j]=data[i] + (data[i+1] * 0x10000);
        //		swap32(&data[j]);
        qDebug("%d: %lx", j, data[j]);
        j++;
    }
    qDebug("----------");
    return ret;
}


/*!
    \fn vmUsb::readBuffer(unsigned short* data)
 */
int VMUSB::readBuffer(unsigned short* data)
{
    qDebug("vmUsb::readBuffer()");
//	return xxusb_bulk_read(hUsbDevice, data, 10000, 100);
    QMutexLocker locker(&m_lock);
    if (!isOpen())
        return -3;
    return xxusb_usbfifo_read(hUsbDevice, (int*)data, 10000, 100);}

/*!
    \fn vmUsb::readLongBuffer(int* data)
 */
int VMUSB::readLongBuffer(int* data)
{
    if (!isOpen())
        return -3;
  return xxusb_bulk_read(hUsbDevice, data, 1200, 100);
}

bool VMUSB::writeActionRegister(uint16_t value)
{
    if (!isOpen())
        return false;

    char outPacket[100];

    // Build up the output packet:

    char* pOut = outPacket;

    pOut = static_cast<char*>(addToPacket16(pOut, 5)); // Select Register block for transfer.
    pOut = static_cast<char*>(addToPacket16(pOut, 10)); // Select action register wthin block.
    pOut = static_cast<char*>(addToPacket16(pOut, value));

    // This operation is write only.

    QMutexLocker locker(&m_lock);
    int outSize = pOut - outPacket;
    int status = usb_bulk_write(hUsbDevice, ENDPOINT_OUT, outPacket, outSize, defaultTimeout_ms);

    if (status != outSize)
    {
        lastUsbError = status;
        return false;
    }

    bool daqMode = (value & 0x1);

    if (m_daqMode != daqMode)
    {
        m_daqMode = daqMode;
        if (daqMode)
            emit daqModeEntered();
        else
            emit daqModeLeft();
        emit daqModeChanged(daqMode);
    }

    return true;
}

/*!
    \fn vmUsb::setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay)
 */
int VMUSB::setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay)
{
    // redundant function to setDaqSettings - allows separate setting of all three components
  u32 val = 0x10000 * frequency + 0x100 * period + delay;
    u32 retval;

  if(writeRegister(8, val) > 0){
    readRegister(8, &retval);
    daqSettings = (int)retval;
  }
  else
    qDebug("Write failed");
  qDebug("timing val = %lx, register: %lx", val, retval);
  return daqSettings;
}


/*!
    \fn vmUsb::setEndianess(bool big)
 */
void VMUSB::setEndianess(bool big)
{
  bigendian = big;
  if(bigendian)
    qDebug("Big Endian");
  else
    qDebug("Little Endian");

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

int VMUSB::listExecute(CVMUSBReadoutList *list, void *readBuffer, size_t readBufferSize, size_t *bytesRead)
{
  size_t outSize;
  uint16_t* outPacket = listToOutPacket(TAVcsWrite | TAVcsIMMED, list, &outSize);

    // Now we can execute the transaction:

  int status = transaction(outPacket, outSize, readBuffer, readBufferSize);

  delete []outPacket;

  if(status >= 0) {
    *bytesRead = status;
  }
  else {
    *bytesRead = 0;
  }
  return status;
}

int VMUSB::listLoad(CVMUSBReadoutList *list, uint8_t stackID, size_t stackMemoryOffset, int timeout_ms)
{
    // Need to construct the TA field, straightforward except for the list number
    // which is splattered all over creation.

    uint16_t ta = TAVcsSel | TAVcsWrite;
    if (stackID & 1)  ta |= TAVcsID0;
    if (stackID & 2)  ta |= TAVcsID1; // Probably the simplest way for this
    if (stackID & 4)  ta |= TAVcsID2; // few bits.

    size_t   packetSize;
    uint16_t *outPacket = listToOutPacket(ta, list, &packetSize, stackMemoryOffset);

    QMutexLocker locker(&m_lock);
    if (!isOpen())
        return -3;
    int status = usb_bulk_write(hUsbDevice, ENDPOINT_OUT,
                                reinterpret_cast<char *>(outPacket),
                                packetSize, timeout_ms);

    delete []outPacket;

    if (status < 0)
    {
#ifdef __MINGW32__
        qDebug("vmUsb::listLoad: usb write failed with code %d", status);
#else
        char errorBuffer[256] = {};
        char *buf = strerror_r(-status, errorBuffer, sizeof(errorBuffer));
        qDebug("vmUsb::listLoad: usb write failed with code %d: %s", status, buf);
#endif
        errno = -status;
    }

    return status;
}

/*
   Utility function to perform a 'symmetric' transaction.
   Most operations on the VM-USB are 'symmetric' USB operations.
   This means that a usb_bulk_write will be done followed by a
   usb_bulk_read to return the results/status of the operation requested
   by the write.
   Parametrers:
   void*   writePacket   - Pointer to the packet to write.
   size_t  writeSize     - Number of bytes to write from writePacket.

   void*   readPacket    - Pointer to storage for the read.
   size_t  readSize      - Number of bytes to attempt to read.


   Returns:
     > 0 the actual number of bytes read into the readPacket...
         and all should be considered to have gone well.
     -1  The write failed with the reason in errno.
     -2  The read failed with the reason in errno.
*/
int
VMUSB::transaction(void* writePacket, size_t writeSize,
        void* readPacket,  size_t readSize, int timeout_ms)
{
  /*
    qDebug("vmUsb::transaction: writeSize=%u, readSize=%u, timeout_ms=%d",
            writeSize, readSize, timeout_ms);
            */

    QMutexLocker locker(&m_lock);
    if (!isOpen())
    {
        return -3;
    }
    int status = usb_bulk_write(hUsbDevice, ENDPOINT_OUT,
            static_cast<char*>(writePacket), writeSize,
            timeout_ms);

    char errorBuffer[256] = {};

    if (status < 0) {
#ifdef __MINGW32__
        qDebug("vmUsb::transaction: usb write failed with code %d", status);
#else
        char *buf = strerror_r(-status, errorBuffer, sizeof(errorBuffer));
        qDebug("vmUsb::transaction: usb write failed with code %d: %s", status, buf);
#endif
        errno = -status;
        return -1;		// Write failed!!
    }

    status = usb_bulk_read(hUsbDevice, ENDPOINT_IN,
            static_cast<char*>(readPacket), readSize, timeout_ms);

    if (status < 0) {
#ifdef __MINGW32__
        qDebug("vmUsb::transaction: usb read failed with code %d", status);
#else
        char *buf = strerror_r(-status, errorBuffer, sizeof(errorBuffer));
        qDebug("vmUsb::transaction: usb read failed with code %d: %s", status, buf);
        errno = -status;
#endif
        return -2;
    }

    return status;
}

int VMUSB::bulkRead(void *outBuffer, size_t outBufferSize, int timeout_ms)
{
    QMutexLocker locker(&m_lock);
    if (!isOpen())
    {
        return -3;
    }

    int status = usb_bulk_read(hUsbDevice, ENDPOINT_IN,
                               static_cast<char *>(outBuffer), outBufferSize, timeout_ms);

    return status;
}

int
VMUSB::stackWrite(u8 stackNumber, u32 loadOffset, const QVector<u32> &stackData)
{
    CVMUSBReadoutList stackList(stackData);
    return listLoad(&stackList, stackNumber, loadOffset);
}

QPair<QVector<u32>, u32>
VMUSB::stackRead(u8 stackID)
{
    auto ret = qMakePair(QVector<u32>(), 0);

    u16 ta = TAVcsSel;
    if (stackID & 1)  ta |= TAVcsID0;
    if (stackID & 2)  ta |= TAVcsID1; // Probably the simplest way for this
    if (stackID & 4)  ta |= TAVcsID2; // few bits.

    QMutexLocker locker(&m_lock);

    if (!isOpen())
        return ret;

    int status = usb_bulk_write(hUsbDevice, ENDPOINT_OUT, reinterpret_cast<char *>(&ta), sizeof(ta), 100);

    if (status <= 0)
    {
        qDebug("stackRead: usb_bulk_write failed");
        return ret;
    }

    u16 inBuffer[2048] = {};

    int bytesRead = usb_bulk_read(hUsbDevice, ENDPOINT_IN, reinterpret_cast<char *>(&inBuffer), sizeof(inBuffer), 100);

    if (bytesRead <= 0)
    {
        qDebug("stackRead: usb_bulk_read failed");
        return ret;
    }

    qDebug("stackRead begin");
    for (size_t i=0; i<bytesRead/sizeof(u16); ++i)
    {
        qDebug("  %04x", inBuffer[i]);
    }
    qDebug("stackRead end");

    ret.second = inBuffer[1];

    u32 *bufp = reinterpret_cast<u32 *>(inBuffer + 2);
    u32 *endp = reinterpret_cast<u32 *>(inBuffer + bytesRead/sizeof(u16));

    while (bufp < endp)
    {
        ret.first.append(*bufp++);
    }

    return ret;
}

QVector<u32>
VMUSB::stackExecute(const QVector<u32> &stackData, size_t resultMaxWords)
{
    QVector<u32> ret(resultMaxWords);
    CVMUSBReadoutList stackList(stackData);
    size_t bytesRead = 0;
    int status = listExecute(&stackList, ret.data(), ret.size() * sizeof(u32), &bytesRead);
    ret.resize(bytesRead / sizeof(u32));
    return ret;
}

int VMUSB::write32(u32 address, u32 value, u8 amod)
{
    return vmeWrite32(address, value, amod);
}

int VMUSB::write16(u32 address, u16 value, u8 amod)
{
    return vmeWrite16(address, value, amod);
}

int VMUSB::read32(u32 address, u32 *value, u8 amod)
{
    size_t bytesRead = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRead32(address, amod);
    int result = listExecute(&readoutList, value, sizeof(*value), &bytesRead);
    return result;

}

int VMUSB::read16(u32 address, u16 *value, u8 amod)
{
    size_t bytesRead = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRead16(address, amod);
    int result = listExecute(&readoutList, value, sizeof(*value), &bytesRead);
    return result;

}

int VMUSB::bltRead(u32 address, u32 transfers, QVector<u32> *dest, u8 amod, bool fifo)
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
    int result = listExecute(&readoutList, dest->data(), dest->size() * sizeof(u32), &bytesRead);

    if (result >= 0)
    {
        dest->resize(bytesRead / sizeof(u32));
    }

    return result;
}

bool VMUSB::enterDaqMode()
{
    qDebug() << __PRETTY_FUNCTION__;
    return writeActionRegister(1);
}

bool VMUSB::leaveDaqMode()
{
    qDebug() << __PRETTY_FUNCTION__;
    return writeActionRegister(0);
}

bool VMUSB::openFirstDevice()
{
    return openFirstUsbDevice();
}

void VMUSB::close()
{
    closeUsbDevice();
}

bool VMUSB::tryErrorRecovery()
{
    QMutexLocker locker(&m_lock);

    if (!hUsbDevice)
    {
        return false;
    }

    int bytesRead = 0;
    do
    {
        char buffer[VMUSBBufferSize];
        bytesRead = usb_bulk_read(hUsbDevice, ENDPOINT_IN, buffer, sizeof(buffer), 250);
        qDebug("bulk read returned %d", bytesRead);
    } while (bytesRead > 0);

    return leaveDaqMode();
}
