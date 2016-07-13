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
    void setRtData(RealtimeData *rt);
    void setChannelSpectro(ChannelSpectro *channelSpectro)
    { m_channelSpectro = channelSpectro; }
    quint32 *getRingBuffer() const { return m_pRingBuffer; }

private:
    void run();
    QTimer* crunchTimer;
    mvme* myMvme;
    Histogram* m_pHistogram;
    RealtimeData* m_pRtD;
    quint32 m_resolution;
    quint32 m_channels;
    bool m_rtDiag;
    ChannelSpectro *m_channelSpectro;
    bool m_newEvent;
    quint32* m_pRingBuffer;
    quint32 m_readPointer;
    quint16 m_bufferCounter;

signals:
    void bufferStatus(int);

public slots:
    void crunchTimerSlot();
    void initRingbuffer(quint32 bufferSize);
    void newEvent();


};

#endif // DATACRUNCHER_H
