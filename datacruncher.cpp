#include "datacruncher.h"
#include <QTimer>
#include "mvme.h"
#include "mvmedefines.h"
#include "histogram.h"
#include "realtimedata.h"
#include "channelspectro.h"
#include <QDebug>

DataCruncher::DataCruncher(QObject *parent)
    : QThread(parent)
    , m_channelSpectro(0)
{
    setObjectName("DataCruncher");

    myMvme = (mvme*)parent;
    crunchTimer = new QTimer();
    connect(crunchTimer, SIGNAL(timeout()), SLOT(crunchTimerSlot()));
    m_newEvent = false;
    m_rtDiag = true;
    crunchTimer->start(1);
}

void DataCruncher::run(){
    qDebug() << "DataCruncher thread:" << QThread::currentThread();
    //crunchTimer->start(50);
}

void DataCruncher::crunchTimerSlot()
{
    //qDebug() << "DataCruncher: " << QThread::currentThread();

    quint32 values, modId, channel, val, i;

    // anything to do?
    if(!m_newEvent)
        return;

    m_newEvent = false;

    //qDebug("events waiting: %d", m_bufferCounter);

    while(m_bufferCounter){
//        qDebug("header: %lx", m_pRingBuffer[m_readPointer]);
        values = m_pRingBuffer[m_readPointer++];

        if((values & 0xFF000000) != 0x40000000)
        {
            qDebug("header error");
        }
        modId = (values & 0x00FF0000) >> 16;

//        qDebug("modId: %d", modId);

        // extract no. of following data words
        values &= 0x000003FF;

//        qDebug("read %d data entries", values);

        // now extract data
        for(i=1; i< values; i++, m_readPointer++)
        {
            if (m_readPointer > RINGBUFMAX)
            {
                m_readPointer = 0;
            }
//            qDebug("data %d: %lx", i, m_pRingBuffer[m_readPointer]);
            channel = (m_pRingBuffer[m_readPointer] & 0x001F0000) >> 16;
            val = m_pRingBuffer[m_readPointer] & 0x00001FFF;

//            if(channel == 0)
//                qDebug("chan %d, val %d, pos: %d", channel, val, channel*m_resolution + val);

            if((m_pRingBuffer[m_readPointer] & 0xF0000000) == 0x10000000 ||
                    (m_pRingBuffer[m_readPointer] & 0xFF800000) == 0x04000000)
            {
                m_pHistogram->m_data[channel*m_resolution + val]++;

                if (m_channelSpectro)
                {
                    // FIXME: scaling hack. this needs to know the channels resolution
                    // to dynamically downscale
                    m_channelSpectro->setValue(channel, val * (1024.0/8192.0));
                }
            }

            if(m_rtDiag){
                m_pRtD->insertData(channel, (quint16)val);
            }
        }
        // read buffer terminator:
        values = m_pRingBuffer[m_readPointer];

        if ((values & 0xC0000000) == 0xC0000000)
        {
            ++m_readPointer;
            //qDebug("buffer terminator: %x", values);
        }

        m_bufferCounter--;

        emit bufferStatus((int)m_bufferCounter/10);

        if(m_readPointer > RINGBUFMAX)
            m_readPointer = 0;
//        qDebug("readPointer: %d", m_readPointer);
    }
//    crunchTimer->stop();
}

void DataCruncher::initRingbuffer(quint32 bufferSize)
{
    m_pRingBuffer = new quint32[bufferSize];
    m_readPointer = 0;
    m_bufferCounter = 0;
}

void DataCruncher::newEvent()
{
//    qDebug("new event called");
    m_bufferCounter++;
    m_newEvent = true;
    emit bufferStatus((int)m_bufferCounter/10);
//    qDebug("new event - counter %d", m_bufferCounter);
}

DataCruncher::~DataCruncher()
{
    crunchTimer->stop();
    delete crunchTimer;
}

void DataCruncher::setHistogram(Histogram *hist)
{
    m_pHistogram = hist;
    m_channels = hist->m_channels;
    m_resolution = hist->m_resolution;
    qDebug("dc: %d x %d", m_channels, m_resolution);
}

void DataCruncher::setRtData(RealtimeData *rt)
{
    m_pRtD = rt;
}
