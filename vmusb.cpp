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
#include "usb.h"
#include "vmusb.h"

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
    if (numDevices <= 0) {
      return false;
    }


    hUsbDevice = xxusb_device_open(pUsbDevice[0].usbdev);
    if(hUsbDevice != NULL){
        qDebug("success");
		return true;
    }
    else{
        qDebug("fail");
        return false;
    }
}



/*!
    \fn vmUsb::getAllRegisters(void)
 */
void vmUsb::readAllRegisters(void)
{
	long val;
	VME_register_read(hUsbDevice, 0, &val);
	firmwareId = (int) val;
    qDebug("Id: %x",firmwareId);
	VME_register_read(hUsbDevice, 4, &val);
	globalMode = (int)val & 0xFFFF;
	VME_register_read(hUsbDevice, 8, &val);
	daqSettings = (int)val;
	VME_register_read(hUsbDevice, 12, &val);
	ledSources = (int)val;
	VME_register_read(hUsbDevice, 16, &val);
	deviceSources = (int)val;
	VME_register_read(hUsbDevice, 20, &val);
	dggAsettings = (int)val;
	VME_register_read(hUsbDevice, 24, &val);
	dggBsettings = (int)val;
	VME_register_read(hUsbDevice, 28, &val);
	scalerAdata = (int)val;
	VME_register_read(hUsbDevice, 32, &val);
	scalerBdata = (int)val;
	VME_register_read(hUsbDevice, 36, &val);
	numberMask = (int)val;
	VME_register_read(hUsbDevice, 40, &val);
	irqV[0] = (int)val;
	VME_register_read(hUsbDevice, 44, &val);
	irqV[1] = (int)val;
	VME_register_read(hUsbDevice, 48, &val);
	irqV[2] = (int)val;
	VME_register_read(hUsbDevice, 52, &val);
	irqV[3] = (int)val;
	VME_register_read(hUsbDevice, 56, &val);
	extDggSettings = (int)val;
	VME_register_read(hUsbDevice, 60, &val);
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
    \fn vmUsb::checkUsbDevices(void)
 */
void vmUsb::checkUsbDevices(void)
{
//	numDevices = xxusb_devices_find(pUsbDevice);
    short DevFound = 0;
    xxusb_device_type *xxdev;
    usb_dev_handle *udev;
    struct usb_bus *bus;
    struct usb_device *dev;
    struct usb_bus *usb_busses;
    char string[256];
    short ret;
    usb_init();
    usb_find_busses();
    usb_find_devices();
    usb_busses=usb_get_busses();
    for (bus=usb_busses; bus; bus = bus->next)
    {
        qDebug("bus");
        for (dev = bus->devices; dev; dev= dev->next)
         {
             qDebug("device: %x", dev->descriptor.idVendor);
             if(1)
//             if (dev->descriptor.idVendor==XXUSB_WIENER_VENDOR_ID)
//             if (dev->descriptor.idVendor==0x058f)
             {
                 udev = usb_open(dev);
                 if (udev)
                 {
                    for(uint n = 0; n < 16; n++){
//                        qDebug("device %x successfully opened: %d", dev->descriptor.idVendor,dev->descriptor.iSerialNumber);
//                        ret = usb_get_string_simple(udev, dev->descriptor.iSerialNumber, string, sizeof(string));
                        strcpy(string, "");
                        ret = usb_get_string_simple(udev, n, string, sizeof(string));
                        qDebug("device %x (%d) %d: %d %s", dev->descriptor.idVendor, dev->descriptor.iSerialNumber, n, ret, string);
                        if (ret > 0)
                        {
 //                            xxdev[DevFound].usbdev=dev;
  //                         strcpy(xxdev[DevFound].SerialString, string);
//                             qDebug("device string: %s ",string);
                             DevFound++;
                        }
                    }
                    usb_close(udev);
                }
                else
                     qDebug("device %x not opened", dev->descriptor.idVendor);
            }
        }
    }
   qDebug("devices found: %d",DevFound);
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
     struct usb_bus *usb_busses;
     char string[256];
     short ret;

     usb_init();
     usb_find_busses();
     usb_find_devices();
     usb_busses=usb_get_busses();

    for (bus=usb_busses; bus; bus = bus->next)
    {
        for (dev = bus->devices; dev; dev= dev->next)
        {
            qDebug("descriptor: %d %d",dev->descriptor.idVendor, XXUSB_WIENER_VENDOR_ID);
            if (dev->descriptor.idVendor==XXUSB_WIENER_VENDOR_ID)
            {
                qDebug("found wiener device");
                udev = usb_open(dev);
                qDebug("result of open: %d", udev);
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
	\fn vmUsb::getIrq()
 */
int vmUsb::getIrq(int vec)
{
	return irqV[vec];
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

/*!
	\fn vmUsb::setIrq()
 */
int vmUsb::setIrq(int vec, int val)
{
	int pos = 0;
	int regval = 0;
	int addr = 0;
	long int retval;
	
	switch(vec){
		case 0:
			regval = val;
			pos = 0;
			addr = 40; 
			break;
		case 1:
			regval = 0x10000 * val;
			pos = 0;
			addr = 40; 
			break;
		case 2:
			regval = val;
			pos = 1;
			addr = 44; 
			break;
		case 3:
			regval = 0x10000 * val;
			pos = 1;
			addr = 44; 
			break;
		case 4:
			regval = val;
			pos = 2;
			addr = 48; 
			break;
		case 5:
			regval = 0x10000 * val;
			pos = 2;
			addr = 48; 
			break;
		case 6:
			regval = val;
			pos = 3;
			addr = 52; 
			break;
		case 7:
			regval = 0x10000 * val;
			pos = 3;
			addr = 52; 
			break;
	}
	if(VME_register_write(hUsbDevice, addr, regval) > 0){
		VME_register_read(hUsbDevice, addr, &retval);
		irqV[pos] = (int)retval;
	}
	return irqV[pos];
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



/*!
    \fn vmUsb::vmeWrite16(short am, long addr, long data)
 */
short vmUsb::vmeWrite16(long addr, long data)
{
	long intbuf[1000];
	short ret;
//	qDebug("Write16");
	data &= 0xFFFF;
//	swap16(&data);
	intbuf[0]=7;
	intbuf[1]=0;
	intbuf[2]=0x09; 
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
	return ret;
}

/*!
    \fn vmUsb::vmeWrite32(short am, long addr, long data)
 */
short vmUsb::vmeWrite32(long addr, long data)
{
	long intbuf[1000];
	short ret;
//    qDebug("Write32 %lx %lx", addr, data);
//	swap32(&data);
	intbuf[0]=7;
	intbuf[1]=0;
	intbuf[2]=0x09; 
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
	intbuf[2]= (0x09 + 0x100); 
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
	intbuf[2]= (0x09 + 0x100); 
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
	long intbuf[1000];
	short ret;
	int i=0;
//	qDebug("ReadBlt32");
	if (count > 255) return -1;
	intbuf[0]=5;
	intbuf[1]=0;
	intbuf[2]=0x0B | 0x100;
	intbuf[3]=(count << 8);
	if(bigendian)
		intbuf[3] |= 0x1; // little endianess -> PC
	intbuf[4]=(addr & 0xffff);
	intbuf[5]=((addr >>16) & 0xffff);
//    for(int i=0;i<intbuf[0];i++)
//		qDebug("%lx", intbuf[i]);
	ret = xxusb_stack_execute(hUsbDevice, intbuf);
	int j=0;
	for (i=0;i<(2*count);i=i+2)
	{
		data[j]=intbuf[i] + (intbuf[i+1] * 0x10000);
//		swap32(&data[j]);
		j++;
	}
	return ret;
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
		qDebug("  %d: %lx", i, data[i]);
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

/*!
    \fn vmUsb::usbRegisterWrite(int addr, int value)
 */
int vmUsb::usbRegisterWrite(int addr, int value)
{
	return xxusb_register_write(hUsbDevice, addr, value);
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
	VME_register_write(hUsbDevice, 0x10, ledSources);
	
	// scaler parameters:
	VME_register_write(hUsbDevice, 0x8, 0xFF00);
	
	// switch off acquisition
	usbRegisterWrite(1, 0);	
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
