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


vmUsb::vmUsb()
{
  bigendian = false;
}


vmUsb::~vmUsb()
{
}

/*!
    \fn vmUsb::openUsbDevice(void)
 */
bool vmUsb::openUsbDevice(void)
{
/*
    hUsbDevice = usb_open(pUsbDevice[0].usbdev);
    ret = usb_set_configuration(hUsbDevice,1);
    ret = usb_claim_interface(hUsbDevice,0);
// RESET USB (added 10/16/06 Andreas Ruben)
    ret=xxusb_register_write(hUsbDevice, 10, 0x04);
    if(hUsbDevice)
        return true;
    else
        return false;
*/
    if (numDevices <= 0)
    {
        getUsbDevices();
    }

    if (numDevices <= 0)
    {
        throw std::runtime_error("No VM_USB devices found");
    }


    hUsbDevice = xxusb_device_open(pUsbDevice[0].usbdev);

    return hUsbDevice;
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

/*!
    \fn vmUsb::getAllRegisters(void)
 */
void vmUsb::readAllRegisters(void)
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
  numberMask = 0;
  irqV[0] = 0;
  irqV[1] = 0;
  irqV[2] = 0;
  irqV[3] = 0;
  extDggSettings = 0;
  usbBulkSetup = 0;

  long val = 0;
  int status = 0;

  status = VME_register_read(hUsbDevice, 0, &val);
  if (status > 0)
    firmwareId = (int) val;
  qDebug("Id: %x",firmwareId);


  status = VME_register_read(hUsbDevice, 4, &val);
  if (status > 0)
    globalMode = (int)val & 0xFFFF;

  qDebug("globalMode: %x", globalMode);

  status = VME_register_read(hUsbDevice, 8, &val);
  if (status > 0)
    daqSettings = (int)val;

  status = VME_register_read(hUsbDevice, 12, &val);
  if (status > 0)
    ledSources = (int)val;

  status = VME_register_read(hUsbDevice, 16, &val);
  if (status > 0)
    deviceSources = (int)val;

  status = VME_register_read(hUsbDevice, 20, &val);
  if (status > 0)
    dggAsettings = (int)val;

  status = VME_register_read(hUsbDevice, 24, &val);
  if (status > 0)
    dggBsettings = (int)val;

  status = VME_register_read(hUsbDevice, 28, &val);
  if (status > 0)
    scalerAdata = (int)val;

  status = VME_register_read(hUsbDevice, 32, &val);
  if (status > 0)
    scalerBdata = (int)val;

  status = VME_register_read(hUsbDevice, 36, &val);
  if (status > 0)
    numberMask = (int)val;

  status = VME_register_read(hUsbDevice, 40, &val);
  if (status > 0)
    irqV[0] = (int)val;

  status = VME_register_read(hUsbDevice, 44, &val);
  if (status > 0)
    irqV[1] = (int)val;

  status = VME_register_read(hUsbDevice, 48, &val);
  if (status > 0)
    irqV[2] = (int)val;

  status = VME_register_read(hUsbDevice, 52, &val);
  if (status > 0)
    irqV[3] = (int)val;

  status = VME_register_read(hUsbDevice, 56, &val);
  if (status > 0)
    extDggSettings = (int)val;

  status = VME_register_read(hUsbDevice, 60, &val);
  if (status > 0)
    usbBulkSetup = (int)val;
}




/*!
    \fn vmUsb::closeUsbDevice(void)
 */
void vmUsb::closeUsbDevice(void)
{
  xxusb_device_close(hUsbDevice);
}


/*!
    \fn vmUsb::getUsbDevices(void)
 */

 void vmUsb::getUsbDevices(void)
 {

     short DevFound = 0;
     usb_dev_handle *udev;
     struct usb_bus *bus;
     struct usb_device *dev;
     struct usb_bus *usb_busses_;
     char string[256];
     short ret;

     usb_init();
     usb_find_busses();
     usb_find_devices();
     usb_busses_=usb_get_busses();

    for (bus=usb_busses_; bus; bus = bus->next)
    {
        for (dev = bus->devices; dev; dev= dev->next)
        {
            //qDebug("descriptor: %d %d",dev->descriptor.idVendor, XXUSB_WIENER_VENDOR_ID);

            if (dev->descriptor.idVendor==XXUSB_WIENER_VENDOR_ID)
            {
                qDebug("found wiener device");
                udev = usb_open(dev);
                //qDebug("result of open: %d", udev);
                if (udev)
                {
                    qDebug("opened wiener device");
                    ret = usb_get_string_simple(udev, dev->descriptor.iSerialNumber, string, sizeof(string));
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


/*!
    \fn vmUsb::getUsbDevices(void)
 */
/*
 *void vmUsb::getUsbDevices(void)
{
    numDevices = xxusb_devices_find(pUsbDevice);
}
*/

//
// "get" functions for register field
//

/*!
    \fn vmUsb::getFirmwareId()
 */
int vmUsb::getFirmwareId()
{
  return firmwareId;
}

/*!
    \fn vmUsb::getMode()
 */
int vmUsb::getMode()
{
  return globalMode;
}

/*!
  \fn vmUsb::getDaqSettings()
 */
int vmUsb::getDaqSettings()
{
  return daqSettings;
}
/*!
  \fn vmUsb::getLedSources()
 */
int vmUsb::getLedSources()
{
  return ledSources;
}

/*!
  \fn vmUsb::getDeviceSources()
 */
int vmUsb::getDeviceSources()
{
  return deviceSources;
}

/*!
  \fn vmUsb::getDggA()
 */
int vmUsb::getDggA()
{
  return dggAsettings;
}

/*!
  \fn vmUsb::getDggB()
 */
int vmUsb::getDggB()
{
  return dggBsettings;
}

/*!
  \fn vmUsb::getScalerAdata()
 */
int vmUsb::getScalerAdata()
{
  return scalerAdata;
}

/*!
  \fn vmUsb::getScalerBdata()
 */
int vmUsb::getScalerBdata()
{
  return scalerBdata;
}

/*!
  \fn vmUsb::getNumberMask()
 */
int vmUsb::getNumberMask()
{
  return numberMask;
}

/*!
  \fn vmUsb::getDggSettings()
 */
int vmUsb::getDggSettings()
{
  return extDggSettings;
}

/*!
  \fn vmUsb::getUsbSettings()
 */
int vmUsb::getUsbSettings()
{
  return usbBulkSetup;
}

//
// "set" functions for register field
//
/*!
    \fn vmUsb::setFirmwareId()
 */
int vmUsb::setFirmwareId(int val)
{
  if(VME_register_write(hUsbDevice, 0, val) > 0){
    VME_register_read(hUsbDevice, 0, &retval);
    firmwareId = (int)retval;
  }
  return firmwareId;
}

/*!
    \fn vmUsb::setMode()
 */
int vmUsb::setMode(int val)
{
  if(VME_register_write(hUsbDevice, 4, val) > 0){
    VME_register_read(hUsbDevice, 4, &retval);
    globalMode = (int)retval & 0xFFFF;
  }
  return globalMode;
}

/*!
  \fn vmUsb::setDaqSettings()
 */
int vmUsb::setDaqSettings(int val)
{
  if(VME_register_write(hUsbDevice, 8, val) > 0){
    VME_register_read(hUsbDevice, 8, &retval);
    daqSettings = (int)retval;
  }
  return daqSettings;
}
/*!
  \fn vmUsb::setLedSources()
 */
int vmUsb::setLedSources(int val)
{
  if(VME_register_write(hUsbDevice, 12, val) > 0){
    VME_register_read(hUsbDevice, 12, &retval);
    ledSources = (int)retval;
  }
  qDebug("set value: %x", ledSources);
  return ledSources;
}

/*!
  \fn vmUsb::setDeviceSources()
 */
int vmUsb::setDeviceSources(int val)
{
  if(VME_register_write(hUsbDevice, 16, val) > 0){
    VME_register_read(hUsbDevice, 16, &retval);
    deviceSources = (int)retval;
  }
  return deviceSources;
}

/*!
  \fn vmUsb::setDggA()
 */
int vmUsb::setDggA(int val)
{
  if(VME_register_write(hUsbDevice, 20, val) > 0){
    VME_register_read(hUsbDevice, 20, &retval);
    dggAsettings = (int)retval;
  }
  return dggAsettings;
}

/*!
  \fn vmUsb::setDggB()
 */
int vmUsb::setDggB(int val)
{
  if(VME_register_write(hUsbDevice, 24, val) > 0){
    VME_register_read(hUsbDevice, 24, &retval);
    dggBsettings = (int)retval;
  }
  return dggBsettings;
}

/*!
  \fn vmUsb::setScalerAdata()
 */
int vmUsb::setScalerAdata(int val)
{
  if(VME_register_write(hUsbDevice, 28, val) > 0){
    VME_register_read(hUsbDevice, 28, &retval);
    scalerAdata = (int)retval;
  }
  return scalerAdata;
}

/*!
  \fn vmUsb::setScalerBdata()
 */
int vmUsb::setScalerBdata(int val)
{
  if(VME_register_write(hUsbDevice, 32, val) > 0){
    VME_register_read(hUsbDevice, 32, &retval);
    scalerBdata = (int)retval;
  }
  return scalerBdata;
}

/*!
  \fn vmUsb::setNumberMask()
 */
int vmUsb::setNumberMask(int val)
{
  if(VME_register_write(hUsbDevice, 36, val) > 0){
    VME_register_read(hUsbDevice, 36, &retval);
    numberMask = (int)retval;
  }
  return numberMask;
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
int vmUsb::setIrq(int vec, uint16_t val)
{
    int regAddress = irq_vector_register_address(vec);

    if (regAddress < 0)
        throw std::runtime_error("setIrq: invalid vector number given");

    long regValue = 0;

    if (regAddress >= 0 && VME_register_read(hUsbDevice, regAddress, &regValue))
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

        if (VME_register_write(hUsbDevice, regAddress, regValue) &&
                VME_register_read(hUsbDevice, regAddress, &regValue))
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
uint16_t vmUsb::getIrq(int vec)
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
int vmUsb::setDggSettings(int val)
{
  if(VME_register_write(hUsbDevice, 56, val) > 0){
    VME_register_read(hUsbDevice, 56, &retval);
    extDggSettings = (int)retval;
  }
  return extDggSettings;
}

/*!
  \fn vmUsb::setUsbSettings(int val)
 */
int vmUsb::setUsbSettings(int val)
{
  if(VME_register_write(hUsbDevice, 60, val) > 0){
    VME_register_read(hUsbDevice, 60, &retval);
    usbBulkSetup = (int)retval;
  }
  return usbBulkSetup;
}


short vmUsb::vmeWrite16(long addr, long data)
{
    return vmeWrite16(addr, data, VME_AM_A32_USER_PROG);
}

/*!
    \fn vmUsb::vmeWrite16(short am, long addr, long data)
 */
short vmUsb::vmeWrite16(long addr, long data, uint8_t amod)
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

short vmUsb::vmeWrite32(long addr, long data)
{
    return vmeWrite32(addr, data, VME_AM_A32_USER_PROG);
}

short vmUsb::vmeWrite32(long addr, long data, uint8_t amod)
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
  ret = xxusb_stack_execute(hUsbDevice, intbuf);
  return ret;
}

/*!
    \fn vmUsb::vmeRead32(long addr, long* data)
 */
short vmUsb::vmeRead32(long addr, long* data)
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
  ret = xxusb_stack_execute(hUsbDevice, intbuf);
//	qDebug("read: %d %lx %lx", ret, intbuf[0], intbuf[1]);
  *data = intbuf[0] + (intbuf[1] * 0x10000);
//	swap32(data);
  return ret;
}

/*!
    \fn vmUsb::vmeRead16(short am, long addr, long* data)
 */
short vmUsb::vmeRead16(long addr, long* data)
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
int vmUsb::vmeBltRead32(long addr, int count, quint32* data)
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

    /*
    if (status < 0)
        return status;
    */

    return bytesRead;
#endif
}

int vmUsb::vmeMbltRead32(long addr, int count, quint32 *data)
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

    listExecute(&readoutList, data, count * sizeof(quint64), &bytesRead);

    return bytesRead;
#endif
}

/*!
    \fn vmUsb::swap32(long val)
 */
void vmUsb::swap32(long* val)
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
void vmUsb::swap16(long* val)
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
int vmUsb::stackWrite(int id, long* data)
{
  qDebug("StackWrite: id=%d, stackSize=%d", id, data[0]);

  for(int i=0;i<=data[0];i++)
    qDebug("  %d: %lx", i, data[i]);

  unsigned char addr[8] = {2, 3, 18, 19, 34, 35, 50, 51};
  int ret = xxusb_stack_write(hUsbDevice, addr[id], data);
  qDebug("StackWrite %d (addr: %d), %d bytes", id, addr[id], ret);
  return ret;
}


/*!
    \fn vmUsb::stackRead(long* data)
 */
int vmUsb::stackRead(int id, long* data)
{
  unsigned char addr[8] = {2, 3, 18, 19, 34, 35, 50, 51};
  qDebug("StackRead %d (addr: %d)", id, addr[id]);

  int ret = xxusb_stack_read(hUsbDevice, addr[id], data);

  return ret;
}


/*!
    \fn vmUsb::stackExecute(long* data)
 */
int vmUsb:: stackExecute(long* data)
{
    int ret, i, j;
    qDebug("StackExecute: stackSize=%d", data[0]);

    for(i=0;i<=data[0];i++)
        qDebug("  %d: %08lx", i, data[i]);

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
int vmUsb::readBuffer(unsigned short* data)
{
    qDebug("vmUsb::readBuffer()");
//	return xxusb_bulk_read(hUsbDevice, data, 10000, 100);
    return xxusb_usbfifo_read(hUsbDevice, (int*)data, 10000, 100);}

/*!
    \fn vmUsb::readLongBuffer(int* data)
 */
int vmUsb::readLongBuffer(int* data)
{
  return xxusb_bulk_read(hUsbDevice, data, 1200, 100);
}

void vmUsb::writeActionRegister(uint16_t value)
{
    char outPacket[100];

    // Build up the output packet:

    char* pOut = outPacket;

    pOut = static_cast<char*>(addToPacket16(pOut, 5)); // Select Register block for transfer.
    pOut = static_cast<char*>(addToPacket16(pOut, 10)); // Select action register wthin block.
    pOut = static_cast<char*>(addToPacket16(pOut, value));

    // This operation is write only.

    int outSize = pOut - outPacket;
    int status = usb_bulk_write(hUsbDevice, ENDPOINT_OUT, outPacket, outSize, defaultTimeout_ms);

    if (status < 0)
        throw VMUSB_UsbError(status, "Error writing action register");

    if (status != outSize)
        throw std::runtime_error("usb_bulk_write wrote different size than expected");

    m_daqMode = (value & 0x1);

    if (m_daqMode)
        emit daqModeEntered();
    else
        emit daqModeLeft();

    emit daqModeChanged(m_daqMode);
}

/*!
    \fn vmUsb::initialize()
 */
void vmUsb::initialize()
{
    // set some more or less useful defaults:
    qDebug("initialize");
  // mode:
  // buffer length 128 bytes
  VME_register_write(hUsbDevice, 0x4, 5);

  // IRQ 1 to Stack 2, IRQ map 0
  VME_register_write(hUsbDevice, 40, 0x2100);

    // User LED sources:
  // ty: Out FIFO not empty, red: Event Trig., green: ACQ, by: VME DTACK
  ledSources = 0x04030101;
  VME_register_write(hUsbDevice, 0xC, ledSources);

  // scaler parameters:
  VME_register_write(hUsbDevice, 0x8, 0xFF00);

  // switch off acquisition
  writeActionRegister(0);
}


/*!
    \fn vmUsb::setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay)
 */
int vmUsb::setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay)
{
    // redundant function to setDaqSettings - allows separate setting of all three components
  long val = 0x10000 * frequency + 0x100 * period + delay;

  if(VME_register_write(hUsbDevice, 8, val) > 0){
    VME_register_read(hUsbDevice, 8, &retval);
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
void vmUsb::setEndianess(bool big)
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

int vmUsb::listExecute(CVMUSBReadoutList *list, void *readBuffer, size_t readBufferSize, size_t *bytesRead)
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
  return (status >= 0) ? 0 : status;

}

int vmUsb::listLoad(CVMUSBReadoutList *list, uint8_t stackID, size_t stackMemoryOffset, int timeout_ms)
{
    // Need to construct the TA field, straightforward except for the list number
    // which is splattered all over creation.

    uint16_t ta = TAVcsSel | TAVcsWrite;
    if (stackID & 1)  ta |= TAVcsID0;
    if (stackID & 2)  ta |= TAVcsID1; // Probably the simplest way for this
    if (stackID & 4)  ta |= TAVcsID2; // few bits.

    size_t   packetSize;
    uint16_t *outPacket = listToOutPacket(ta, list, &packetSize, stackMemoryOffset);

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
        return -1;
    }

    return 0;
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
vmUsb::transaction(void* writePacket, size_t writeSize,
        void* readPacket,  size_t readSize, int timeout_ms)
{
  /*
    qDebug("vmUsb::transaction: writeSize=%u, readSize=%u, timeout_ms=%d",
            writeSize, readSize, timeout_ms);
            */


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

    status    = usb_bulk_read(hUsbDevice, ENDPOINT_IN,
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

int vmUsb::bulk_read(void *outBuffer, size_t outBufferSize, int timeout_ms)
{
    int status = usb_bulk_read(hUsbDevice, ENDPOINT_IN,
                               static_cast<char *>(outBuffer), outBufferSize, timeout_ms);

    return status;
}

int
vmUsb::stackWrite(u8 stackNumber, u32 loadOffset, const QVector<u32> &stackData)
{
    CVMUSBReadoutList stackList(stackData);
    return listLoad(&stackList, stackNumber, loadOffset);
}

QPair<QVector<u32>, u32>
vmUsb::stackRead(u8 stackID)
{
    auto ret = qMakePair(QVector<u32>(), 0);

    u16 ta = TAVcsSel;
    if (stackID & 1)  ta |= TAVcsID0;
    if (stackID & 2)  ta |= TAVcsID1; // Probably the simplest way for this
    if (stackID & 4)  ta |= TAVcsID2; // few bits.

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
vmUsb::stackExecute(const QVector<u32> &stackData, size_t resultMaxWords)
{
    QVector<u32> ret(resultMaxWords);
    CVMUSBReadoutList stackList(stackData);
    size_t bytesRead = 0;
    int status = listExecute(&stackList, ret.data(), ret.size() * sizeof(u32), &bytesRead);
    ret.resize(bytesRead / sizeof(u32));
    return ret;
}

size_t vmUsb::executeCommands(VMECommandList *commands, void *readBuffer,
        size_t readBufferSize)
{
    CVMUSBReadoutList vmusbList(*commands);
    size_t bytesRead = 0;

    int result = listExecute(&vmusbList, readBuffer, readBufferSize, &bytesRead);

    // TODO: move this into listExecute
    if (result < 0)
        throw VMUSB_UsbError(result, "execute Commands");

    return bytesRead;
}

void vmUsb::write32(uint32_t address, uint8_t amod, uint32_t value)
{
    vmeWrite32(address, value, amod); // TODO: error reporting via exception in the lower layers
}

void vmUsb::write16(uint32_t address, uint8_t amod, uint16_t value)
{
    vmeWrite16(address, value, amod); // TODO: error reporting via exception in the lower layers
}

uint32_t vmUsb::read32(uint32_t address, uint8_t amod)
{
    size_t bytesRead = 0;
    uint32_t result = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRead32(adress, amod);
    listExecute(&readoutList, &result, sizeof(result), &bytesRead);
    return result;
}

uint16_t vmUsb::read16(uint32_t address, uint8_t amod)
{
    size_t bytesRead = 0;
    uint16_t result = 0;
    CVMUSBReadoutList readoutList;
    readoutList.addRead16(adress, amod);
    listExecute(&readoutList, &result, sizeof(result), &bytesRead);
    return result;
}

void vmUsb::enterDaqMode()
{
    writeActionRegister(1);
}

void vmUsb::leaveDaqMode()
{
    writeActionRegister(0);
}
