#ifndef HISTOGRAM_H
#define HISTOGRAM_H

#include <QObject>
#include <QTextStream>

class QPointF;

class HistogramCollection : public QObject
{
    Q_OBJECT
public:
    explicit HistogramCollection(QObject *parent = 0, quint32 channels=32, quint32 resolution=8192);
    ~HistogramCollection();

    void resize(quint32 channels, quint32 resolution);

    void initHistogram(void);
    void clearChan(quint32 chan);
    void clearHistogram(void);
    void calcStatistics(quint32 chan, quint32 start=0, quint32 stop=0);

    double getValue(quint32 channelIndex, quint32 valueIndex);
    bool incValue(quint32 channelIndex, quint32 valueIndex);

    quint64 getMaxTotalCount() const;

    double *m_data = 0;
    double *m_axisBase = 0;
    quint32 m_channels;
    quint32 m_resolution;

#define MAX_CHANNEL_COUNT 50

    double m_maxchan[MAX_CHANNEL_COUNT];
    double m_maximum[MAX_CHANNEL_COUNT];
    double m_mean[MAX_CHANNEL_COUNT];
    double m_sigma[MAX_CHANNEL_COUNT];
    double m_counts[MAX_CHANNEL_COUNT];
    double m_overflow[MAX_CHANNEL_COUNT];
    quint64 m_totalCounts[MAX_CHANNEL_COUNT];

signals:

public slots:

private:

};

//QTextStream &writeHistogramCollection(QTextStream &out, HistogramCollection *histo);
//QTextStream &readHistogramCollectionInto(QTextStream &in, HistogramCollection *histo);

QTextStream &writeHistogram(QTextStream &out, HistogramCollection *histo, quint32 channelIndex);
//QTextStream &readHistogram(QTextStream &in, HistogramCollection *histo, quint32 *channelIndexOut = nullptr);

#endif // HISTOGRAM_H
