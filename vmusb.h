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
#ifndef VMUSB_H
#define VMUSB_H

#include <qobject.h>
#include <libxxusb.h>



/**
represents vm_usb controller

	@author Gregor Montermann <g.montermann@mesytec.com>
*/
class vmUsb : public QObject
{

public:
    vmUsb();

    ~vmUsb();
    void readAllRegisters(void);
    bool openUsbDevice(void);
    void closeUsbDevice(void);
    void getUsbDevices(void);
    void checkUsbDevices(void);

	int getFirmwareId();
	int getMode();
	int getDaqSettings();
	int getLedSources();
	int getDeviceSources();
	int getDggA();
	int getDggB();
	int getScalerAdata();
	int getScalerBdata();
	int getNumberMask();
	int getIrq(int vec);
	int getDggSettings();
	int getUsbSettings();
	
	int setFirmwareId(int val);
	int setMode(int val);
	int setDaqSettings(int val);
	int setLedSources(int val);
	int setDeviceSources(int val);
	int setDggA(int val);
	int setDggB(int val);
	int setScalerAdata(int val);
	int setScalerBdata(int val);
	int setNumberMask(int val);
	int setIrq(int vec, int val);
	int setDggSettings(int val);
	int setUsbSettings(int val);
    short vmeWrite32(long addr, long data);
    short vmeRead32(long addr, long* data);
	short vmeWrite16(long addr, long data);
	short vmeRead16(long addr, long* data);
    int vmeBltRead32(long addr, int count, quint32* data);
	void swap32(long* val);
	void swap16(long* val);
    int stackWrite(int id, long* data);
    int stackRead(int id, long* data);
    int stackExecute(long* data);
    int readBuffer(unsigned short* data);
    int readLongBuffer(int* data);
    int usbRegisterWrite(int addr, int value);
    void initialize();
    int setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay);
	void setEndianess(bool big);
	
	xxusb_device_type pUsbDevice[5];
    char numDevices;
    usb_dev_handle* hUsbDevice;
    short ret;

protected:
	int firmwareId;
	int globalMode;
	int daqSettings;
	int ledSources;
	int deviceSources;
	int dggAsettings;
	int dggBsettings;
	int scalerAdata;
	int scalerBdata;
	int numberMask;
	int irqV[4];
	int extDggSettings;
	int usbBulkSetup;
    long int retval;
	bool bigendian;

};
#endif
