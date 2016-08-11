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

#include <libxxusb.h>
#include <util.h>
#include <QObject>
#include <QPair>
#include <QVector>

#include "vmecontroller.h"
#include "vmusb_constants.h"

class CVMUSBReadoutList;


/**
represents vm_usb controller

  @author Gregor Montermann <g.montermann@mesytec.com>
*/
class VMUSB: public VMEController
{
    Q_OBJECT
    signals:
        void daqModeEntered();
        void daqModeLeft();
        void daqModeChanged(bool);

    public:
        VMUSB();
        ~VMUSB();

        virtual bool isOpen() const { return hUsbDevice; }
        QString getSerialNumber() const;

        void readAllRegisters(void);
        bool openFirstUsbDevice(void);
        void closeUsbDevice(void);
        void getUsbDevices(void);

        void enterDaqMode();
        void leaveDaqMode();

        bool isInDaqMode() const { return m_daqMode; }
        int getFirmwareId();
        int getMode();
        int getDaqSettings();
        int getLedSources();
        int getDeviceSources();
        int getDggA();
        int getDggB();
        int getScalerAdata();
        int getScalerBdata();
        u32 getEventsPerBuffer();
        uint16_t getIrq(int vec);
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
        u32 setEventsPerBuffer(u32 val);
        int setIrq(int vec, uint16_t val);
        int setDggSettings(int val);
        int setUsbSettings(int val);
        u32 readRegister(u32 address);
        short vmeWrite32(long addr, long data);
        short vmeWrite32(long addr, long data, uint8_t amod);
        short vmeRead32(long addr, long* data);
        short vmeWrite16(long addr, long data);
        short vmeWrite16(long addr, long data, uint8_t amod);
        short vmeRead16(long addr, long* data);
        int vmeBltRead32(long addr, int count, quint32* data);
        int vmeMbltRead32(long addr, int count, quint32* data);
        void swap32(long* val);
        void swap16(long* val);
        int stackWrite(int id, long* data);
        int stackRead(int id, long* data);
        int stackExecute(long* data);
        int readBuffer(unsigned short* data);
        int readLongBuffer(int* data);

        void writeActionRegister(uint16_t value);

        int setScalerTiming(unsigned int frequency, unsigned char period, unsigned char delay);
        void setEndianess(bool big);

        /* Executes the given stack (in the form of a readout list) and reads the
         * response into readBuffer. The actual number of bytes read is stored in
         * bytesRead. */
        int listExecute(CVMUSBReadoutList *list, void *readBuffer, size_t readBufferSize, size_t *bytesRead);

        /* Loads the given stack to stackID using the given memory offset. */
        int listLoad(CVMUSBReadoutList *list, uint8_t stackID, size_t stackMemoryOffset, int timeout_ms = 1000);

        int stackWrite(u8 stackNumber, u32 loadOffset, const QVector<u32> &stackData);
        // returns the stack words and the stack load offset
        QPair<QVector<u32>, u32> stackRead(u8 stackNumber);
        QVector<u32> stackExecute(const QVector<u32> &stackData, size_t resultMaxWords=1024);

        /* Executes the commands in commandList and reads the response into readBuffer.
         * Returns the number of bytes read. Throws on error. */
        virtual size_t executeCommands(VMECommandList *commands, void *readBuffer,
                size_t readBufferSize) override;


        /* Writes the given writePacket to the VM_USB and reads the response back into readPacket. */
        int transaction(void* writePacket, size_t writeSize,
                void* readPacket,  size_t readSize, int timeout_ms = 1000);

        int bulkRead(void *outBuffer, size_t outBufferSize, int timeout_ms = 1000);

        xxusb_device_type pUsbDevice[5];
        char numDevices = 0;
        usb_dev_handle* hUsbDevice = nullptr;
        short ret; // TODO: is this used?

        //
        // VMEController interface
        //

        virtual VMEControllerType getType() const { return VMEControllerType::VMUSB; }

        virtual void write32(uint32_t address, uint8_t amod, uint32_t value);
        virtual void write16(uint32_t address, uint8_t amod, uint16_t value);

        virtual uint32_t read32(uint32_t address, uint8_t amod);
        virtual uint16_t read16(uint32_t address, uint8_t amod);

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
        u32 eventsPerBuffer;
        int irqV[4];
        int extDggSettings;
        int usbBulkSetup;
        long int retval;
        bool bigendian = false;
        bool m_daqMode = false;
        QString m_currentSerialNumber;

        // timeout used for all operations except daq mode bulk transfers
        // TODO: use these everywhere
        size_t defaultTimeout_ms = 100;
        size_t bulkTimeout_ms = 100;
};

// Constants:

// Identifying marks for the VM-usb:

static const short USB_WIENER_VENDOR_ID(0x16dc);
static const short USB_VMUSB_PRODUCT_ID(0xb);

// Bulk transfer endpoints

static const int ENDPOINT_OUT(2);
static const int ENDPOINT_IN(0x86);

// Timeouts:

static const int DEFAULT_TIMEOUT(2000);	// ms.

// Retries for flushing the fifo/stopping data taking:

static const int DRAIN_RETRIES(5);    // Retries.

// The register offsets:

static const unsigned int FIDRegister(0);       // Firmware id.
static const unsigned int GMODERegister(4);     // Global mode register.
static const unsigned int DAQSetRegister(8);    // DAQ settings register.
static const unsigned int LEDSrcRegister(0xc);	// LED source register.
static const unsigned int DEVSrcRegister(0x10);	// Device source register.
static const unsigned int DGGARegister(0x14);   // GDD A settings.
static const unsigned int DGGBRegister(0x18);   // GDD B settings.
static const unsigned int ScalerA(0x1c);        // Scaler A counter.
static const unsigned int ScalerB(0x20);        // Scaler B data.
static const unsigned int ExtractMask(0x24);    // CountExtract mask.
static const unsigned int ISV12(0x28);          // Interrupt 1/2 dispatch.
static const unsigned int ISV34(0x2c);          // Interrupt 3/4 dispatch.
static const unsigned int ISV56(0x30);          // Interrupt 5/6 dispatch.
static const unsigned int ISV78(0x34);          //  Interrupt 7/8 dispatch.
static const unsigned int DGGExtended(0x38);    // DGG Additional bits.
static const unsigned int USBSetup(0x3c);       // USB Bulk transfer setup.
static const unsigned int USBVHIGH1(0x40);       // additional bits of some of the interrupt vectors.
static const unsigned int USBVHIGH2(0x44);       // additional bits of the other interrupt vectors.


// Bits in the list target address word:

static const uint16_t TAVcsID0(1); // Bit mask of Stack id bit 0.
static const uint16_t TAVcsSel(2); // Bit mask to select list dnload
static const uint16_t TAVcsWrite(4); // Write bitmask.
static const uint16_t TAVcsIMMED(8); // Target the VCS immediately.
static const uint16_t TAVcsID1(0x10);
static const uint16_t TAVcsID2(0x20);
static const uint16_t TAVcsID12MASK(0x30); // Mask for top 2 id bits
static const uint16_t TAVcsID12SHIFT(4);

namespace TransferSetupRegister
{
    static const uint32_t multiBufferCountMask   = 0xff;
    static const uint32_t multiBufferCountShift  = 0;

    static const uint32_t timeoutMask            = 0xf00;
    static const uint32_t timeoutShift           = 8;
}

namespace ISVWord // half of a ISV register
{
    static const uint32_t stackIDShift  = 12;
    static const uint32_t irqLevelShift = 8;
}

namespace DaqSettingsRegister
{
    static const uint32_t ScalerReadoutFrequencyShift = 16;
    static const uint32_t ScalerReadoutFrequencyMask  = 0xffff0000;
    static const uint32_t ScalerReadoutPerdiodShift   = 8;
    static const uint32_t ScalerReadoutPerdiodMask    = 0x0000ff00;
    static const uint32_t ReadoutTriggerDelayShift    = 0;
    static const uint32_t ReadoutTriggerDelayMask     = 0x000000ff;
}

namespace GlobalModeRegister
{
    static const uint32_t MixedBufferShift = 5;
    static const uint32_t MixedBufferMask  = 0x00000020;
}

uint16_t*
listToOutPacket(uint16_t ta, CVMUSBReadoutList* list,
                        size_t* outSize, off_t offset = 0);


struct VMUSB_DeviceNotOpen: public std::runtime_error
{
    VMUSB_DeviceNotOpen()
        : std::runtime_error("VMUSB device is not open")
    {}
};

struct VMUSB_UsbError: public std::runtime_error
{
    VMUSB_UsbError(int usb_result)
        : std::runtime_error("VMUSB USB Error")
          , usb_result(usb_result)
    {}

    VMUSB_UsbError(int usb_result, const char *message)
        : std::runtime_error(message)
          , usb_result(usb_result)
    {}

    QString toString() const
    {
        return QString("%1, error code = %2")
            .arg(what())
            .arg(usb_result);
    }

    int usb_result;
};

#endif
