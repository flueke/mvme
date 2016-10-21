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

#ifdef __MINGW32__
#include <lusb0_usb.h>
#else
#include <usb.h>
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

static usb_dev_handle *open_usb_device(struct usb_device *dev)
{
    usb_dev_handle *udev;
    udev = usb_open(dev);

    if (!udev) return NULL;

    int res = usb_set_configuration(udev,1);

    if (res < 0)
    {
        qDebug("usb_set_configuration failed");
        usb_close(udev);
        return NULL;
    }

    res = usb_claim_interface(udev,0);

    if (res < 0)
    {
        qDebug("usb_claim_interface failed");
        usb_close(udev);
        return NULL;
    }

    return udev;
}

static void close_usb_device(usb_dev_handle *devHandle)
{
    int res = usb_release_interface(devHandle, 0);

    if (res < 0)
    {
        qDebug("usb_release_interface failed");
    }

    usb_close(devHandle);
}

VMUSB::VMUSB()
    : m_state(ControllerState::Closed)
    , m_lock(QMutex::Recursive)
{
}


VMUSB::~VMUSB()
{
    close();
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
    qDebug("writeRegister response: value=0x%08x, bytes=%d", value, bytesRead);
    return ret;
}

#if 0
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
#endif




/*!
    \fn vmUsb::getUsbDevices(void)
 */

 void VMUSB::getUsbDevices(void)
{
    struct usb_bus *bus;
    struct usb_device *dev;
    struct usb_bus *usb_busses_;

    deviceInfos.clear();

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
                usb_dev_handle *udev = usb_open(dev);
                //qDebug("result of open: %d", udev);
                if (udev)
                {
                    qDebug("opened wiener device");

                    vmusb_device_info info;
                    info.usbdev = dev;

                    int status = usb_get_string_simple(udev, dev->descriptor.iSerialNumber, info.serial, sizeof(info.serial));

                    if (status < 0)
                        info.serial[0] = '\0';

                    deviceInfos.push_back(info);

                    qDebug("serial=%s", info.serial);

                    usb_close(udev);
                }
            }
        }
    }
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
    if(!writeRegister(0, val).isError())
    {
        u32 retval = 0;
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
    if(!writeRegister(4, val).isError())
    {
        u32 retval;
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
    if(!writeRegister(8, val).isError())
    {
        u32 retval;
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
    if(!writeRegister(12, val).isError()){
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
  if(!writeRegister(16, val).isError()){
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
  if(!writeRegister(20, val).isError()){
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
  if(!writeRegister(24, val).isError()){
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
  if(!writeRegister(28, val).isError()){
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
  if(!writeRegister(32, val).isError()){
    readRegister(32, &retval);
    scalerBdata = (int)retval;
  }
  return scalerBdata;
}

u32 VMUSB::setEventsPerBuffer(u32 val)
{
    u32 retval;
  if(!writeRegister(36, val).isError()){
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
    {
        qDebug("setIrq: invalid vector number given");
        return -1;
    }

    u32 regValue = 0;

    if (regAddress >= 0 && !readRegister(regAddress, &regValue).isError())
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

        if (!writeRegister(regAddress, regValue).isError() &&
                !readRegister(regAddress, &regValue).isError())
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
  if(!writeRegister(56, val).isError()){
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
  if(!writeRegister(60, val).isError()){
    readRegister(60, &retval);
    usbBulkSetup = (int)retval;
  }
  return usbBulkSetup;
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

  if(!writeRegister(8, val).isError()){
    readRegister(8, &retval);
    daqSettings = (int)retval;
  }
  else
    qDebug("Write failed");
  qDebug("timing val = %lx, register: %lx", val, retval);
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
        errno = -status;
        qDebug("vmUsb::listLoad: usb write failed with code %d: %s", status,
               errnoString(errno).toLocal8Bit().constData());
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


    int status = usb_bulk_write(hUsbDevice, ENDPOINT_OUT,
            static_cast<char*>(writePacket), writeSize,
            timeout_ms);

    if (status < 0)
        return VMEError(VMEError::WriteError, status, errnoString(-status));

    status = usb_bulk_read(hUsbDevice, ENDPOINT_IN,
            static_cast<char*>(readPacket), readSize, timeout_ms);

    if (status < 0)
        return VMEError(VMEError::ReadError, status, errnoString(-status));

    *bytesRead = status;

    return {};
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

    u16 response = 0;
    size_t bytesRead = 0;
    auto error = listExecute(&readoutList, &response, sizeof(response), &bytesRead);

    if (error.isError())
        return error;

    if (response == 0)
        return VMEError(VMEError::BusError, QSL("No DTACK on write"));

    return {};
}

VMEError VMUSB::write16(u32 address, u16 value, u8 amod)
{
    CVMUSBReadoutList readoutList;
    readoutList.addWrite16(address, amod, value);

    u16 response = 0;
    size_t bytesRead = 0;
    auto error = listExecute(&readoutList, &response, sizeof(response), &bytesRead);

    if (error.isError())
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

VMEError VMUSB::openFirstDevice()
{
    QMutexLocker locker(&m_lock);

    if (isOpen())
        return VMEError(VMEError::DeviceIsOpen);

    getUsbDevices();

    if (deviceInfos.isEmpty())
        return VMEError(VMEError::NoDevice);

    auto deviceInfo = deviceInfos[0];

    hUsbDevice = open_usb_device(deviceInfo.usbdev);

    if (hUsbDevice)
    {
        m_currentSerialNumber = deviceInfo.serial;

        writeActionRegister(0x04); // reset usb

        // clear the action register (makes sure daq mode is disabled)
        if (tryErrorRecovery())
        {
            m_state = ControllerState::Opened;
            emit controllerOpened();
            emit controllerStateChanged(m_state);
            return VMEError();
        }
        else
        {
            qDebug() << "vmusb error recovery failed";
            close();
            return VMEError(VMEError::CommError);
        }
    }

    return VMEError(VMEError::CommError, errnoString(errno));
}

VMEError VMUSB::close()
{
    QMutexLocker locker (&m_lock);
    if (hUsbDevice)
    {
        close_usb_device(hUsbDevice);
        hUsbDevice = nullptr;
        m_currentSerialNumber = QString();
        m_state = ControllerState::Closed;
        emit controllerClosed();
        emit controllerStateChanged(m_state);
    }
    return {};
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
