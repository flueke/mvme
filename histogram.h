#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <QObject>
#include <QTextStream>

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

    double getValue(quint32 channelIndex, quint32 valueIndex);
    bool incValue(quint32 channelIndex, quint32 valueIndex);

    void setValue(quint32 channelIndex, quint32 valueIndex, double value);
    void setAxisBaseValue(quint32 valueIndex, double value);

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
    double m_overflow[MAX_CHANNEL_COUNT];

signals:

public slots:

private:

};

QTextStream &writeHistogramCollection(QTextStream &out, Histogram *histo);
QTextStream &readHistogramCollectionInto(QTextStream &in, Histogram *histo);

QTextStream &writeHistogram(QTextStream &out, Histogram *histo, quint32 channelIndex);
QTextStream &readHistogram(QTextStream &in, Histogram *histo, quint32 *channelIndexOut = nullptr);

#endif // HISTOGRAM_H
