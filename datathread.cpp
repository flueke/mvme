#include <cassert>
#include "datathread.h"
#include "mvme.h"
#include "math.h"
#include <QTimer>
#include "mvmedefines.h"
#include "datacruncher.h"
#include "CVMUSBReadoutList.h"
#include "vme.h"
#include <QDebug>
#include <QMutexLocker>
#include <QTime>
#include "util.h"


#define DATABUFFER_SIZE 100000

#ifdef QT_NO_DEBUG
#define ENABLE_DEBUG_TEXT_LISTFILE 0
#define ENABLE_DEBUG_BIN_LISTFILE 0
#else
#define ENABLE_DEBUG_TEXT_LISTFILE 1
#define ENABLE_DEBUG_BIN_LISTFILE 1
#include <QStandardPaths>
#endif

DataThread::DataThread(QObject *parent)
    : QObject(parent)
    , m_baseAddress(0x0)
    , m_outputFile(0)
    , m_inputFile(0)
{
    setObjectName("DataThread");

    myMvme = (mvme*)parent;
    dataTimer = new QTimer(this);
    m_multiEvent = false;
    m_readLength = 100;
    initBuffers();
    connect(dataTimer, SIGNAL(timeout()), SLOT(dataTimerSlot()));

}

/* This is the central data readout function. It is called
 * periodically. If data available, it reads out according to the
 * specs of the vme devices involved
*/
#ifdef VME_CONTROLLER_CAEN
void DataThread::dataTimerSlot()
{
    qDebug() << "DataThread: " << QThread::currentThread();
    quint32 i, j, len, id;
    quint32 ret;

    // read available data from controller
    // todo: implement multiple readout depending on list of devices
    // todo: implement readout routines depending on type of vme device
    ret = readData();

    qDebug("received %d words: %d", ret, m_readLength);
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

#elif defined VME_CONTROLLER_WIENER
void DataThread::dataTimerSlot()
{

    //qDebug() << "DataThread: " << QThread::currentThread();


    // read available data from controller
    // todo: implement multiple readout depending on list of devices
    // todo: implement readout routines depending on type of vme device
    int ret = readData();

    if (ret <= 0 && m_inputFile && m_inputFile->atEnd())
        return;

    ++m_debugTransferCount;

    //qDebug("received %d bytes, m_readLength=%d", ret, m_readLength);

    if (ret < 0)
    {
        qDebug("dataTimerSlot: readData returned error code %d", ret);
        return;
    }

    if(ret == 0)
    {
        //qDebug("dataTimerSlot: no data received, returning");
        return;
    }

    quint32 wordsReceived = ret / sizeof(quint32);

#if 0
    qDebug("buffer dump:");
    for(quint32 bufferIndex = 0; bufferIndex < wordsReceived; ++bufferIndex)
    {
        qDebug("  0x%08lx", dataBuffer[bufferIndex]);
    }
    qDebug("end of buffer dump");
#endif

    // todo: replace by adequate function call in vme module class
    if(!checkData())
        return;

    if (!m_daqMode)
    {
#if 1
        for(quint32 bufferIndex = 0; bufferIndex < wordsReceived; ++bufferIndex)
        {
            quint32 currentWord = dataBuffer[bufferIndex];

            if (currentWord != 0xFFFFFFFF && currentWord != 0x00000000)
            {
                m_pRingbuffer[m_writePointer++] = currentWord;

                if (m_writePointer > RINGBUFMAX)
                {
                    m_writePointer = 0;
                }
            }
        }

        emit dataReady();

#else
        enum BufferState
        {
            BufferState_Header,
            BufferState_Data,
            BufferState_EOE
        };

        BufferState bufferState = BufferState_Header;
        quint32 wordsInEvent = 0;

        for(quint32 bufferIndex = 0; bufferIndex < wordsReceived; ++bufferIndex)
        {
            quint32 currentWord = dataBuffer[bufferIndex];

            // skip BERR markers inserted by VMUSB and fillwords
            if (currentWord == 0xFFFFFFFF || currentWord == 0x00000000)
            {
                continue;
            }

            switch (bufferState)
            {
            case BufferState_Header:
            {
                if ((currentWord & 0xC0000000) == 0x40000000)
                {

                    wordsInEvent = currentWord & 0x000003FF;
                    m_pRingbuffer[m_writePointer++] = currentWord;
                    bufferState = BufferState_Data;

                    //qDebug("found header word 0x%08lx, wordsInEvent=%u", currentWord, wordsInEvent);
                } else
                {
                    qDebug("transfer=%u: did not find header word, skipping to next word. got 0x%08lx",
                           m_debugTransferCount-1, currentWord);
                    //debugOutputBuffer(dataBuffer, wordsReceived);
                }
            } break;

            case BufferState_Data:
            {
                bool data_found_flag = ((currentWord & 0xF0000000) == 0x10000000) // MDPP
                        || ((currentWord & 0xFF800000) == 0x04000000); // MxDC

                if (!data_found_flag)
                {
                    qDebug("warning: data_found_flag not set: 0x%08lx",
                           currentWord);
                }

                m_pRingbuffer[m_writePointer++] = currentWord;

                if (--wordsInEvent == 1)
                {
                    bufferState = BufferState_EOE;
                }
            } break;

            case BufferState_EOE:
            {
                /* Note: MADC sometimes seems to not output EOE words (and possibly others). */
                if ((currentWord & 0xC0000000) == 0xC0000000)
                {
                    //qDebug("found EOE: 0x%08lx", currentWord);
                } else
                {
                    qDebug("transfer=%u, expected EOE word, got 0x%08lx, continuing regardless (previous word=0x%08lx, next word=0x%08lx)",
                           m_debugTransferCount-1,
                           dataBuffer[bufferIndex], dataBuffer[bufferIndex-1], dataBuffer[bufferIndex+1]);

                    --bufferIndex; // try the word again as a header word
                }

                m_pRingbuffer[m_writePointer++] = currentWord;
                bufferState = BufferState_Header;
                emit dataReady();
            } break;
            }

            if (m_writePointer > RINGBUFMAX)
            {
                m_writePointer = 0;
            }
        }

        if (bufferState != BufferState_Header)
        {
            qDebug("transfer=%u, warning: bufferState != BufferState_Header after loop",
                   m_debugTransferCount-1);
        }
#endif
    }
    else
    {
        // daq mode
        try
        {
            //qDebug("transfer=%u: read %d bytes, %d shorts, %d longs",
            //        m_debugTransferCount-1, ret, ret/sizeof(u16), ret/sizeof(u32));

            int globalMode = myVu->getMode();

            VMUSB_Buffer buffer(reinterpret_cast<u8 *>(dataBuffer), ret, globalMode);

            u32 header1 = buffer.extractWord();

            bool lastBuffer = (header1 >> 15) & 1;
            bool scalerBuffer = (header1 >> 14) & 1;
            bool continuousMode = (header1 >> 13) & 1;
            bool multiBuffer = (header1 >> 12) & 1;
            u16 numberOfEvents = header1 & 0xFFF;

            if (lastBuffer || scalerBuffer || continuousMode || multiBuffer)
            {
                qDebug("header1: lastBuffer=%d, scalerBuffer=%d, continuousMode=%d, multiBuffer=%d, numberOfEvents=%u",
                        lastBuffer, scalerBuffer, continuousMode, multiBuffer, numberOfEvents);
            }

            if (buffer.headerOpt())
            {
                u32 header2 = buffer.extractWord();
                u16 numberOfWords = header2 & 0xFFF;
                //qDebug("header2: numberOfWords=%u", numberOfWords);
            }

            for (u32 eventIndex=0; eventIndex < numberOfEvents; ++eventIndex)
            {
                u32 eventHeader = buffer.extractWord();

                u8 stackId = (eventHeader >> 13) & 7;
                bool partialEvent = (eventHeader >> 12) & 0x1;
                u16 eventLength = eventHeader & 0xFFF;

                u32 dataWordsInEvent = (eventLength / sizeof(u16));
                u32 bytesAfterData  = eventLength % sizeof(u16);

                if (partialEvent || bytesAfterData)
                {
                    qDebug("eventHeader=%08x, stackId=%u, partialEvent=%d, eventLength=%u, dataWordsInEvent=%u, bytesAfterData=%u",
                            eventHeader, stackId, partialEvent, eventLength, dataWordsInEvent, bytesAfterData);
                }

                for (u32 i=0; i<dataWordsInEvent; ++i)
                {
                    u32 data = buffer.extractU32();
                    //qDebug("  data word %d: %08x", i, data);

                    if (!scalerBuffer && data != 0xFFFFFFFF && data != 0x00000000)
                    {
                        m_pRingbuffer[m_writePointer++] = data;
                        if (m_writePointer > RINGBUFMAX)
                        {
                            m_writePointer = 0;
                        }
                    }
                }

                if (!scalerBuffer)
                {
                    emit dataReady();
                }
            }

            u32 bufferTerminator1 = buffer.extractWord();
            u32 bufferTerminator2 = buffer.extractWord();

            //qDebug("bufferTerminator1=%08x, bufferTerminator2=%08x, bytesLeft=%u",
            //        bufferTerminator1, bufferTerminator2, buffer.endp - buffer.buffp);

        } catch (const end_of_buffer &)
        {
            qDebug("error: end of buffer reached unexpectedly!");
        }
    }
}
#endif

DataThread::~DataThread()
{
    delete dataTimer;
    delete dataBuffer;
}

void DataThread::startReading(quint16 readTimerPeriod)
{
    QMutexLocker locker(&m_controllerMutex);

    dataTimer->setInterval(readTimerPeriod);

    if (!m_inputFile && m_daqMode)
    {
        myVu->usbRegisterWrite(1, 1);
    }

#if ENABLE_DEBUG_TEXT_LISTFILE
    {
        QString fileName = QString("%1/mvme-debug-text-list.txt")
                .arg(QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0));

        m_debugTextListFile.setFileName(fileName);
        m_debugTextListFile.open(QIODevice::WriteOnly);
        m_debugTextListStream.setDevice(&m_debugTextListFile);
    }
#endif

    m_debugTransferCount = 0;

    /* Start the timer in the DataThreads thread context. If start() would be
     * called directly, the timer events would be generated in the current
     * thread context (the GUI thread). Thus if the GUI is busy not enough timer events
     * would be generated, slowing down the readout. */
    QMetaObject::invokeMethod(dataTimer, "start");
}

void DataThread::stopReading()
{
    QMetaObject::invokeMethod(dataTimer, "stop");

    QTime time;
    time.start();
    QMutexLocker locker(&m_controllerMutex);
    qDebug("stopReading: mutex lock took %d ms", time.elapsed());
    qDebug("stopReading: transfer count = %u", m_debugTransferCount);

    if (!m_inputFile)
    {
        if (m_daqMode)
        {
            int result = myVu->usbRegisterWrite(1, 0);
            qDebug("disable daq mode result: %d", result);

            do {
                // FIXME: this data is valid and needs to be processed. also a
                // check for 'last buffer' bit should be done
                qDebug("performing usb read to clear remaning data");
                char buffer[27 * 1024];
                result = usb_bulk_read(myVu->hUsbDevice, XXUSB_ENDPOINT_IN, buffer, 27 * 1024, 100);
                qDebug("bulk read returned %d", result);
            } while (result > 0);
        }
    }

    if (m_inputFile)
    {
        m_inputFile->close();
        delete m_inputFile;
        m_inputFile = nullptr;
    }

    if (m_outputFile)
    {
        m_outputFile->close();
        delete m_outputFile;
        m_outputFile = nullptr;
    }

#if ENABLE_DEBUG_TEXT_LISTFILE
    if (m_debugTextListFile.isOpen())
    {
        m_debugTextListFile.close();
        qDebug() << "debug text list file written to" << m_debugTextListFile.fileName();
    }
#endif

#if ENABLE_DEBUG_BIN_LISTFILE
    if (m_debugBinListFile.isOpen())
    {
        m_debugBinListFile.close();
        qDebug() << "debug bin list file written to" << m_debugBinListFile.fileName();
    }
#endif
}

void DataThread::setRingbuffer(quint32 *buffer)
{
    m_pRingbuffer = buffer;
    m_writePointer = 0;
    qDebug("ringbuffer initialized");
}

void DataThread::setReadoutmode(bool multi, quint16 maxlen, bool mblt, bool daqMode)
{
    QMutexLocker locker(&m_controllerMutex);

    m_mblt = mblt;
    m_multiEvent = multi;
    m_daqMode = daqMode;

    if (daqMode)
        return;

#if defined VME_CONTROLLER_WIENER

    int blt_am = mblt ? VME_AM_A32_USER_MBLT : VME_AM_A32_USER_BLT;

    if (multi)
    {
        m_readLength = 65535;
    }
    else
    {
        m_readLength = 100;
    }

    CVMUSBReadoutList readoutList;

#if 0
    // using a prepared readout list
    if(multi){
        /* Read the mxdc fifo using a block transfer. This should result in a BERR
         * once all data in the FIFO has been read.  */
        readoutList.addFifoRead32(0x00000000, blt_am, m_readLength);

        /* Write to the read_reset register to clear BERR and allow a new conversion. */
        readoutList.addWrite16(0x00000000 | 0x6034, VME_AM_A32_USER_PROG, 1);

    }
    else
    {
        readoutList.addFifoRead32(0x00000000, am, m_readLength);
        readoutList.addWrite16(0x00000000 | 0x6034, VME_AM_A32_USER_PROG, 1);
    }
#endif

#if 0

    // Trying to read the number of transfers from register 6030 and then transfer exactly that amount.
    // Result: 140 bytes per read in single event mode (35 words) but the data is swapped somehow (same as for multi event mode 1).
    readoutList.addBlockCountRead16(0x6030, 0x0000FFFF, VME_AM_A32_USER_PROG);
    readoutList.addMaskedCountFifoRead32(0x0, blt_am);
    readoutList.addWrite16(0x00000000 | 0x6034, VME_AM_A32_USER_PROG, 2);

    m_readoutPacket.reset(listToOutPacket(TAVcsWrite | TAVcsIMMED, &readoutList, &m_readoutPacketSize));
    qDebug("readoutPacketSize=%d", m_readoutPacketSize);
#endif
#endif
}

void DataThread::initBuffers()
{
    dataBuffer = new quint32[DATABUFFER_SIZE];
    qDebug("buffers initialized");
}


int DataThread::readData()
{
    int offset = 0;
#ifdef VME_CONTROLLER_CAEN
    quint16 ret;
    quint8 irql;
    quint16 count = m_readLength*4;

    //check for irq
    irql = myCu->Irq();
    if(irql){
        // read until no further data
        while(count == m_readLength*4){
            if(m_mblt)
                count = myCu->vmeMbltRead32(0x0, m_readLength * 4, &dataBuffer[offset]);
            else
                count = myCu->vmeBltRead32(0x0, m_readLength * 4, &dataBuffer[offset]);

//            qDebug("read %d bytes from USB - %d %d", count, offset, m_readLength*4);
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

#elif defined VME_CONTROLLER_WIENER

    QMutexLocker locker(&m_controllerMutex);

#if 1
    bool inputAtEnd = false;

    if (m_inputFile)
    {
        inputAtEnd = m_inputFile->atEnd();

        uint32_t transferSize = 0;
        if (m_inputFile->read(reinterpret_cast<char *>(&transferSize), sizeof(transferSize)) == sizeof(transferSize))
        {
            if (transferSize > 0)
            {
                if (m_inputFile->read(reinterpret_cast<char *>(dataBuffer), transferSize) == transferSize)
                {
                    offset = transferSize;
                }
            }
        }
    }
    else if (!m_daqMode)
    {
        // read the amount of data in the MXDC FIFO
        long dataLen = 0;
        short status = myVu->vmeRead16(m_baseAddress | 0x6030, &dataLen);

        if (status > 0 && dataLen > 0)
        {
            if (m_mblt)
            {
                offset = myVu->vmeMbltRead32(m_baseAddress, dataLen, dataBuffer);
            } else
            {
                offset = myVu->vmeBltRead32(m_baseAddress, dataLen, dataBuffer);
            }
            // reset module readout
            myVu->vmeWrite16(m_baseAddress | 0x6034, 1);
        }
    }
    else
    {
        offset = xxusb_bulk_read(myVu->hUsbDevice, dataBuffer, 27 * 1024, 5000);
    }

    if (m_outputFile && offset > 0)
    {
        uint32_t transferSize = offset;
        if (m_outputFile->write(reinterpret_cast<char *>(&transferSize), sizeof(transferSize)) == sizeof(transferSize))
        {
            m_outputFile->write(reinterpret_cast<char *>(dataBuffer), transferSize);
        }
    }

    if (!inputAtEnd)
        debugWriteTextListFile(offset);

    return offset;

#else
    int timeout_ms = 250;

    int bytesRead = myVu->transaction(m_readoutPacket.get(), m_readoutPacketSize, dataBuffer, DATABUFFER_SIZE, timeout_ms);

    //qDebug("readData: transaction returned %d", bytesRead);

    if (bytesRead <= 0)
    {
        qDebug("readData: transaction returned %d", bytesRead);
    } else
    {
        debugWriteTextListFile(bytesRead);
    }

    offset = bytesRead > 0 ? bytesRead : 0;
    return offset;
#endif
#endif

}

void DataThread::debugWriteTextListFile(int bytesRead)
{
    if (!m_debugTextListFile.isOpen())
        return;

    if (bytesRead <= 0)
    {
        m_debugTextListStream << "transfer #" << m_debugTransferCount
                              << ", status=" << bytesRead
                              << endl;
    } else
    {
        int wordsRead = bytesRead / sizeof(quint32);
        int bytesLeft = bytesRead % sizeof(quint32);

        m_debugTextListStream << "transfer #" << m_debugTransferCount
                              << ", bytes=" << bytesRead
                              << ", words=" << wordsRead
                              << ", bytes_left=" << bytesLeft
                              << endl;

        int wordIndex = 0;
        for (; wordIndex < wordsRead; ++wordIndex)
        {
            m_debugTextListStream << hex << dataBuffer[wordIndex] << dec << endl;
        }

        for (int byteIndex = 0; byteIndex < bytesLeft; ++byteIndex)
        {
            quint32 byteValue = (quint32)(*((quint8 *)(dataBuffer + wordIndex) + byteIndex));
            m_debugTextListStream << hex << byteValue << dec << endl;
        }
    }
}

#ifdef VME_CONTROLLER_CAEN
void DataThread::setCu(caenusb *cu)
{
    myCu = cu;
}
#else
void DataThread::setVu(vmUsb *vu)
{
    myVu = vu;
}
#endif

#if 0
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
#endif

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

