#ifndef DATATHREAD_H
#define DATATHREAD_H

#include <QObject>
#include <QMutex>
#include <memory>
#include <QTextStream>
#include <QFile>

class QTimer;
class mvme;
class vmUsb;
class caenusb;

class DataThread : public QObject
{
    Q_OBJECT
public:
    explicit DataThread(QObject *parent = 0);
    ~DataThread();
    void initBuffers();
    quint32 readData();
#ifdef VME_CONTROLLER_CAEN
    void setCu(caenusb *cu);
#else
    void setVu(vmUsb* vu);
#endif

#if 0
    quint32 readFifoDirect(quint16 base, quint16 len, quint32* data);
#endif
//    quint32 readBlt32(quint16 base, quint16 len, quint32* data);
    void analyzeBuffer(quint8 type);

    bool checkData();

signals:
    void dataReady();
    void bufferStatus(int);

public slots:
    void dataTimerSlot();
    void startReading(quint16 readTimerPeriod);
    void stopReading();
    void setRingbuffer(quint32* buffer);
    void setReadoutmode(bool multi, quint16 maxlen, bool mblt);
    void setReadoutBaseAddress(quint32 baseAddress)
    {
        m_baseAddress = baseAddress;
    }

protected:
    void debugWriteTextListFile(int bytesRead);


    QTimer* dataTimer;
    mvme* myMvme;
#ifdef VME_CONTROLLER_WIENER
    vmUsb* myVu;
    std::unique_ptr<uint16_t> m_readoutPacket;
    size_t m_readoutPacketSize;
#elif defined VME_CONTROLLER_CAEN
    caenusb* myCu;
#endif

    quint32* dataBuffer;

    quint32* m_pRingbuffer;
    quint32 m_writePointer;

    quint32 bufferCounter;

    quint32 m_baseAddress;


    quint32 rp;
    quint32 wordsToRead;
    quint32 eventsToRead;
    quint32 evWordsToRead;
    quint32 mEvWordsToRead;
    quint16 mBufPointer;
    quint8 readNext;

    bool m_multiEvent;
    bool m_mblt;
    quint16 m_readLength;

    QMutex m_controllerMutex;
    QTextStream m_debugTextListStream;
    QFile m_debugTextListFile;
    size_t m_debugTransferCount;
};

#endif // DATATHREAD_H
