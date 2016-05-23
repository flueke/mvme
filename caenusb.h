#ifndef CAENUSB_H
#define CAENUSB_H

#define LINUX

#include <QObject>
#include "CAENVMElib.h"
#include "CAENVMEtypes.h"

class caenusb : public QObject
{
    Q_OBJECT
public:
    explicit caenusb(QObject *parent = 0);
    
    bool openUsbDevice(void);
    void closeUsbDevice(void);

    int getFirmwareId();
    int getIrq(int vec);

    short vmeWrite32(long addr, long data);
    short vmeRead32(long addr, long* data);
    short vmeWrite16(long addr, long data);
    short vmeRead16(long addr, long* data);
    int vmeMbltRead32(long addr, int count, quint32 *data);
    int vmeBltRead32(long addr, int count, quint32 *data);
    void swap32(long* val);
    void swap16(long* val);
    unsigned int ackIrq(quint8 level);
    unsigned int readBuffer(quint32 *data);
    int readLongBuffer(int* data);
    void initialize();
    void setEndianess(bool big);
    bool Irq();

    unsigned char numDevices;
    int32_t hUsb;
    bool bErr;
    bool mblt;
    long readAddr;
    long resetAddr;


signals:

public slots:

protected:
    CVAddressModifier am;
    CVDataWidth dw;

    int firmwareId;
    long int retval;
    bool bigendian;
    long baseAddress;
    long readLen;

};

#endif // CAENUSB_H
