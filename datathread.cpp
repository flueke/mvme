#include "datathread.h"
#include "mvme.h"
#include "math.h"
#include <QTimer>
#include "mvmedefines.h"
#include "datacruncher.h"

DataThread::DataThread(QObject *parent) :
    QThread(parent)
{
    myMvme = (mvme*)parent;
    dataTimer = new QTimer(this);
    m_multiEvent = false;
    readlen = 100;
    initBuffers();
    connect(dataTimer, SIGNAL(timeout()), SLOT(dataTimerSlot()));
}

void DataThread::run()
{
//    dataTimer->start(49);
}

/* This is the central data readout function. It is called
 * periodically. If data available, it reads out according to the
 * specs of the vme devices involved
*/
void DataThread::dataTimerSlot()
{
    quint32 i, j, len, id;
    quint32 ret;

    // read available data from controller
    // todo: implement multiple readout depending on list of devices
    // todo: implement readout routines depending on type of vme device
    ret = readData();
//    qDebug("received %d words: %d", ret, readlen);
    if(ret <= 0)
        return;

    // todo: replace by adequate function call in vme module class
    if(!checkData())
        return;

    // look for header word:
    if((dataBuffer[0] & 0xF0000000) != 0x40000000){
        qDebug("wrong header word %lx", dataBuffer[0]);
        return;
    }
    // extract data length (# of following words):
    len = (quint32)(dataBuffer[0] & 0x00000FFF);
//    qDebug("read: %d, words: %d", ret, len);

    // copy data into datacruncher ringbuffer
    for(i = 0; i <= len; i++, m_writePointer++){
//        qDebug("%d %x",i, dataBuffer[i]);
        m_pRingbuffer[m_writePointer] = dataBuffer[i];
    }
    if(m_writePointer > RINGBUFMAX)
        m_writePointer = 0;

    emit dataReady();
}

DataThread::~DataThread()
{
    delete dataTimer;
    delete dataBuffer;
}

void DataThread::startDataTimer(quint16 period)
{
    dataTimer->start(period);
}

void DataThread::stopDataTimer(void)
{
    dataTimer->stop();
}

void DataThread::startReading()
{
    // stop acquisition
    myCu->vmeWrite16(0x603A, 0);
    // clear FIFO
    myCu->vmeWrite16(0x603C, 1);
    // start acquisition
    myCu->vmeWrite16(0x603A, 1);
    // readout reset
    myCu->vmeWrite16(0x6034, 1);
}

void DataThread::stopReading()
{
    myCu->vmeWrite16(0x603A, 0);
    myCu->vmeWrite16(0x603C, 1);
}

void DataThread::setRingbuffer(quint32 *buffer)
{
    m_pRingbuffer = buffer;
    m_writePointer = 0;
    qDebug("ringbuffer initialized");
}

void DataThread::setReadoutmode(bool multi, quint16 maxlen, bool mblt)
{
    // stop acquisition
//    myCu->vmeWrite16(0x603A, 0);
    // reset FIFO
//    myCu->vmeWrite16(0x603C, 1);

    if(multi){
        // multievent register
//        qDebug("set multi");
//        myCu->vmeWrite16(0x6036, 1);
//        myCu->vmeWrite16(0x601A, maxlen);
        m_multiEvent = true;
        readlen = maxlen + 34;
    }
    else{
//        qDebug("set single");
//        myCu->vmeWrite16(0x6036, 0);
        m_multiEvent = false;
    }
    m_mblt = mblt;
    // clear Fifo
//    myCu->vmeWrite16(0x603C, 1);
    // reset readout
//    myCu->vmeWrite16(0x6034, 1);
}

void DataThread::initBuffers()
{
    dataBuffer = new quint32[100000];
    qDebug("buffers initialized");
}


quint32 DataThread::readData()
{
    quint16 ret;
    quint8 irql;
    quint16 count = readlen*4;
    quint16 offset = 0;

    //check for irq
    irql = myCu->Irq();
    if(irql){
        // read until no further data
        while(count == readlen*4){
            if(m_mblt)
                count = myCu->vmeMbltRead32(0x0, readlen * 4, &dataBuffer[offset]);
            else
                count = myCu->vmeBltRead32(0x0, readlen * 4, &dataBuffer[offset]);

//            qDebug("read %d bytes from USB - %d %d", count, offset, readlen*4);
            offset += count;
        }
//        qDebug("read %d bytes from USB", offset);
        // service irq
        ret = myCu->ackIrq(irql);
        // reset module readout
        myCu->vmeWrite16(0x6034, 1);
    }
    else
        offset = 0;
    return offset;
}

void DataThread::setVu(vmUsb *vu)
{
    myVu = vu;
}

void DataThread::setCu(caenusb *cu)
{
    myCu = cu;
}

quint32 DataThread::readFifoDirect(quint16 base, quint16 len, quint32 *data)
{
    long longVal;
    quint32 i;
    for(i = 0; i < len; i++){
        myCu->vmeRead32(base, &longVal);
        data[i] = longVal;
    }
    return i;
}

bool DataThread::checkData()
{
    return true;
}

void DataThread::analyzeBuffer(quint8 type)
{
/*    unsigned int oldlen = 0;
    unsigned int totallen = 0;
    unsigned int elen = 0;
    unsigned int dataLen = 0;
    unsigned int * point = (unsigned int*) sDataBuffer;
    unsigned char evnum;
    unsigned int nEvents = 0;

    int ret = 0;
    unsigned int limit = 0;

    bool quit = false;

    QString s, s2;
    int i, j;
    unsigned int start1 = 0xFFFF;
    unsigned int start2 = 0xAAAA;
    bool flag = false;

    qDebug("now reading buffer");

    // read buffer
    ret = myCu->readBuffer(sDataBuffer);
    if(!(ret > 0))
        return;

    qDebug("read Buffer, len: %d", ret);

    rp = 0;
    wordsToRead = ret/2;
    bufferCounter++;

    // read VME buffer header
    nEvents = sDataBuffer[rp] & 0x0FFF;
    s.sprintf("Buffer #%d header: %04x, #bytes: %d, #events: %d\n", bufferCounter, sDataBuffer[rp], ret, nEvents);
    s2.sprintf("first data words (beginning with buffer header): %04x %04x %04x %04x\n", sDataBuffer[rp], sDataBuffer[rp+1], sDataBuffer[rp+2], sDataBuffer[rp+3]);
    s.append(s2);

    wordsToRead--;
    rp++;

    // next should be VME event header
    dataLen = (sDataBuffer[rp] & 0x0FFF);
    evWordsToRead = dataLen;
    s2.sprintf("VME event header: %04x, len:%d\n", sDataBuffer[rp], dataLen);
    s.append(s2);

    wordsToRead--;
    rp++;

        s2.sprintf("now listing complete buffer:\n");
        s.append(s2);
        rp = 0;
        for(unsigned int i = 0; i < (ret / 4); i++){
            dataBuffer[mBufPointer] = sDataBuffer[rp] + 0x10000 * sDataBuffer[rp+1];
            rp += 2;
            wordsToRead-=2;
            evWordsToRead-=2;
            s2.sprintf("%04d   %08x\n", i, dataBuffer[mBufPointer]);
            s.append(s2);
        }

/*
    // next should be mesytec event headers:
    dataBuffer[mBufPointer] = sDataBuffer[rp] + 0x10000 * sDataBuffer[rp+1];
//	qDebug("header: %08x", dataBuffer[mBufPointer]);
    while((dataBuffer[mBufPointer] & 0xC0000000) == 0x40000000){
        elen = (dataBuffer[mBufPointer] & 0x000007FF);
        mEvWordsToRead = elen * 2;
        rp+=2;
        wordsToRead-=2;
        evWordsToRead-=2;
        s2.sprintf("mesytec evHead: %08x, elen:%d, Buf: %d, Ev: %d\n", dataBuffer[mBufPointer], elen, wordsToRead, evWordsToRead);
        s.append(s2);
        mBufPointer++;

        // next should be mesytec events:
        for(unsigned i = 0; i < elen; i++){
            dataBuffer[mBufPointer] = sDataBuffer[rp] + 0x10000 * sDataBuffer[rp+1];
            s2.sprintf("%08x\n", dataBuffer[mBufPointer]);
            s.append(s2);
            rp += 2;
            wordsToRead-=2;
            evWordsToRead-=2;
            mBufPointer++;
        }
        int result = myData->handover(dataBuffer);
        if(result == -1){
            qDebug("Wrong length %d in event# %d, oldlen: %d", elen, evnum, oldlen);
        }
        mBufPointer = 0;
        dataBuffer[mBufPointer] = sDataBuffer[rp] + 0x10000 * sDataBuffer[rp+1];
    }
    rp+=2;
    wordsToRead-=2;
    evWordsToRead-=2;
    if(dataBuffer[mBufPointer] == 0xFFFFFFFF){
        s2.sprintf("Terminator %08x, Buf: %d, Ev: %d\n", dataBuffer[mBufPointer], wordsToRead, evWordsToRead);
        s.append(s2);
    }

    // now wordsToRead should be = 2
    if(wordsToRead == 2){
        s2.sprintf("remaining words to read: %d\n", wordsToRead);
        s.append(s2);
    }
    else{
        s2.sprintf("ERROR: remaining words to read: %d (should be: 2)\n", wordsToRead);
        s.append(s2);
        s2.sprintf("now listing complete buffer:\n");
        s.append(s2);
        rp = 0;
        for(unsigned int i = 0; i < wordsToRead / 2; i++){
            dataBuffer[mBufPointer] = sDataBuffer[rp] + 0x10000 * sDataBuffer[rp+1];
            rp += 2;
            wordsToRead-=2;
            evWordsToRead-=2;
            s2.sprintf("%08x\n", dataBuffer[mBufPointer]);
            s.append(s2);
        }
    }
    // now evWordsToRead should be = 0
    if(evWordsToRead == 0)
        s2.sprintf("remaining event words to read: %d\n", evWordsToRead);
    else
        s2.sprintf("ERROR: remaining event words to read: %d (should be: 0)\n", wordsToRead);
    s.append(s2);

    // next should be vm-USB buffer terminator:
    // misuse dataBuffer for calculation...
    dataBuffer[mBufPointer] = sDataBuffer[rp] + 0x10000 * sDataBuffer[rp+1];
    wordsToRead -= 2;
    if(dataBuffer[mBufPointer] == 0xFFFFFFFF)
        s2.sprintf("Buffer Terminator: %08x, %d, %d\n", dataBuffer[mBufPointer], wordsToRead, evWordsToRead);
    else
        s2.sprintf("ERROR: expected FFFFFFFF, read: %08x\n", dataBuffer[mBufPointer], wordsToRead, evWordsToRead);
    s.append(s2);
*/
//    mctrl->ui->dataDisplay->setText(s);
//    str.sprintf("%d", bufferCounter);
//    mctrl->ui->buffer->setText(str);

        //debugStream << s;
}

