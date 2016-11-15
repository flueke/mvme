#ifndef REALTIMEDATA_H
#define REALTIMEDATA_H

#define CHANPERMOD 36
#define CHANSIZE 2500 // was 10000, but when changing device thresholds it takes too long for the number to catch up

#include <QObject>

class RealtimeData : public QObject
{
    Q_OBJECT
public:
    explicit RealtimeData(QObject *parent = 0);
    ~RealtimeData();
    void clearData();
    void insertData(quint8 chan, quint16 value);
    void calcData(void);
    double getRdMean(quint8 slot);
    double getRdSigma(quint8 slot);
    void setFilter(quint8 lo, quint8 hi);

signals:
    
public slots:

private:
    quint16* m_pRD;
    quint16 m_RDreadPointer[CHANPERMOD];
    quint16 m_RDwritePointer[CHANPERMOD];
    quint16 m_RDbufferCounter[CHANPERMOD];
    double m_RDmean[CHANPERMOD+2];
    double m_RDsigma[CHANPERMOD+2];
    quint8 m_lo;
    quint8 m_hi;
};

#endif // REALTIMEDATA_H
