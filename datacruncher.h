#ifndef DATACRUNCHER_H
#define DATACRUNCHER_H

#include <QThread>
class QTimer;
class mvme;
class Histogram;
class RealtimeData;
class ChannelSpectro;


class DataCruncher : public QThread
{
    Q_OBJECT
public:
    explicit DataCruncher(QObject *parent = 0);
    ~DataCruncher();
    void setHistogram(Histogram *hist);
    void rtDiag(bool on);
    quint32* m_pRingBuffer;
    quint32 m_readPointer;
    quint16 m_bufferCounter;
    bool m_newEvent;
    void setRtData(RealtimeData *rt);
    void setChannelSpectro(ChannelSpectro *channelSpectro)
    { m_channelSpectro = channelSpectro; }

protected:
    void run();
    QTimer* crunchTimer;
    mvme* myMvme;
    Histogram* m_pHistogram;
    RealtimeData* m_pRtD;
    quint32 m_resolution;
    quint32 m_channels;
    bool m_rtDiag;
    ChannelSpectro *m_channelSpectro;

signals:
    void bufferStatus(int);

public slots:
    void crunchTimerSlot();
    void initRingbuffer(quint32 bufferSize);
    void newEvent();


};

#endif // DATACRUNCHER_H
