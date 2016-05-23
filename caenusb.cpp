#include "caenusb.h"
#include "CAENVMElib.h"
#include "CAENVMEtypes.h"

caenusb::caenusb(QObject *parent) :
    QObject(parent)
{
    bigendian = false;
    baseAddress = 0x0000;
    bErr = false;
}

bool caenusb::openUsbDevice(void)
{
    short e, res;
    char rel[10];
    for(int i=0; i<10; i++)
        rel[i] = ' ';
    res = CAENVME_Init(cvV1718, 0, 0, &hUsb);
    switch(res){
    case cvSuccess:
        qDebug("Operation completed successfully");
        break;
    case cvBusError:
        qDebug("VME bus error during the cycle");
        break;
    case cvCommError:
        qDebug("Communication error");
        break;
    case cvGenericError:
        qDebug("Unspecified error");
        break;
    case cvInvalidParam:
        qDebug("Invalid parameter");
        break;
    case cvTimeoutError:
        qDebug("timeout");
        break;
    }

    e = CAENVME_SWRelease(rel);
    qDebug("driver release %d %s", res, rel);

    e = CAENVME_BoardFWRelease(hUsb, rel);
    qDebug("board release %s", rel);

    if(res == 0)
        return true;
    else
        return false;
}



/*!
    \fn caenusb::closeUsbDevice(void)
 */
void caenusb::closeUsbDevice(void)
{
     CAENVME_End(hUsb);
}



/*!
    \fn caenusb::vmeWrite16(short am, long addr, long data)
 */
short caenusb::vmeWrite16(long addr, long data)
{
    am = cvA32_U_DATA;
    dw = cvD16;
    CAENVME_WriteCycle(hUsb, baseAddress + addr, &data, am, dw);
    return 0;
}

/*!
    \fn caenusb::vmeWrite32(short am, long addr, long data)
 */
short caenusb::vmeWrite32(long addr, long data)
{
    am = cvA32_U_DATA;
    dw = cvD32;
    CAENVME_WriteCycle(hUsb, baseAddress + addr, &data, am, dw);
    return 0;
}

/*!
    \fn caenusb::vmeRead32(short am, long addr, long* data)
 */
short caenusb::vmeRead32(long addr, long* data)
{
    am = cvA32_U_DATA;
    dw = cvD32;
    CAENVME_ReadCycle(hUsb, baseAddress + addr, data, am, dw);
    return 0;
}

/*!
    \fn caenusb::vmeRead16(short am, long addr, long* data)
 */
short caenusb::vmeRead16(long addr, long* data)
{
    am = cvA32_U_DATA;
    dw = cvD16;
    CAENVME_ReadCycle(hUsb, baseAddress + addr, data, am, dw);
    return 0;
}

/*!
    \fn caenusb::vmeBltRead32(short am, long addr, ushort count, quint32* data)
 */
int caenusb::vmeBltRead32(long addr, int count, quint32* data)
{
    unsigned short val;
    am = cvA32_U_BLT;
    dw = cvD32;

    CAENVME_BLTReadCycle(hUsb, baseAddress + addr, data, count, am, dw, &count);

    return count;
}

/*!
    \fn caenusb::vmeMbltRead32(short am, long addr, ushort count, long* data)
 */
int caenusb::vmeMbltRead32(long addr, int count, quint32 *data)
{
    unsigned short val;
    am = cvA32_U_MBLT;

    CAENVME_MBLTReadCycle(hUsb, baseAddress + addr, data, count, am, &count);
    return count;
}

/*!
    \fn caenusb::swap32(long val)
 */
void caenusb::swap32(long* val)
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
    \fn caenusb::swap16(long val)
 */
void caenusb::swap16(long* val)
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


unsigned int caenusb::ackIrq(quint8 level)
{
    void * v;
    dw = cvD16;
//    qDebug("ack irq level %x", level);
    return CAENVME_IACKCycle(hUsb, (CVIRQLevels)level, &v, dw);
//    CAENVME_IACKCycle(hUsb, cvIRQ1, &v, dw);
//    return 1;
}


/*!
    \fn caenusb::readBuffer(unsigned short* data)
 */
unsigned int caenusb::readBuffer(quint32* data)
{
    unsigned short offset = 0;
    unsigned short count = 256;
    bool berr = false;
    unsigned short val = 0;

    // answer IRQ
    void * v;
    dw = cvD16;
    CAENVME_IACKCycle(hUsb, cvIRQ1, &v, dw);
    // try to read until BERR occurs
    while(count == 256){
        // read 256 bytes from BLT
//        if(mblt)
//            count = vmeMbltRead32(readAddr, 256, (long*)&data[offset]);
//        else
            count = vmeBltRead32(0, 256, &data[offset]);

//        qDebug("read buffer %d, %d", count, offset);
//        for(unsigned char c=0;c<count/4;c++)
//            qDebug("%d: %x", c+offset, data[c+offset]);
//        offset += count/4;

        // did BERR occur?
//		CAENVME_ReadRegister(hUsb, cvStatusReg, &val);
//		qDebug("Status: %x", val);
//		if(val & 0x0020)
//			berr = true;
    }
/*
    // refresh ADC
    am = cvA32_U_DATA;
    dw = cvD16;
    CAENVME_WriteCycle(hUsb, resetAddr + 0x6034, data, am, dw);
*/
    return((unsigned int) offset);
}

/*!
    \fn caenusb::readLongBuffer(int* data)
 */
int caenusb::readLongBuffer(int* data)
{
}

/*!
    \fn caenusb::initialize()
 */
void caenusb::initialize()
{
    // set some more or less useful defaults:

}



/*!
    \fn caenusb::setEndianess(bool big)
 */
void caenusb::setEndianess(bool big)
{
    bigendian = big;
    if(bigendian)
        qDebug("Big Endian");
    else
        qDebug("Little Endian");

}


/*!
    \fn caenusb::Irq()
 */
bool caenusb::Irq()
{
    unsigned char d;

    CAENVME_IRQCheck(hUsb, &d);

    // if IRQ: read!
//	qDebug("irq: %d", d);
    if(d == cvIRQ1)
        return true;
    else
        return false;
}

