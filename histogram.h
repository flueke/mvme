#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <QObject>

class QPointF;

class Histogram : public QObject
{
    Q_OBJECT
public:
    explicit Histogram(QObject *parent = 0, quint32 channels=32, quint32 resolution=8192);
    ~Histogram();
    void initHistogram(void);
    void clearChan(quint32 chan);
    void clearHistogram(void);
    void calcStatistics(quint32 chan, quint32 start=0, quint32 stop=0);

    quint32 get_val(quint32 x, quint32 y);
    QPointF get_point(quint32 x, quint32 y);
    bool inc_val(quint32 x, quint32 y);
    double *m_data;
    double *m_axisBase;
    quint32 m_channels;
    quint32 m_resolution;

#define MAX_CHANNEL_COUNT 50

    double m_maxchan[MAX_CHANNEL_COUNT];
    double m_maximum[MAX_CHANNEL_COUNT];
    double m_mean[MAX_CHANNEL_COUNT];
    double m_sigma[MAX_CHANNEL_COUNT];
    double m_counts[MAX_CHANNEL_COUNT];

signals:
    
public slots:

private:

};

#endif // HISTOGRAM_H
