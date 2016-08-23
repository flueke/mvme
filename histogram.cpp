#include "histogram.h"
#include "math.h"
#include <cassert>
#include <QDebug>

HistogramCollection::HistogramCollection(QObject *parent, quint32 channels, quint32 resolution)
    : QObject(parent)
{
    resize(channels, resolution);
    initHistogram();
    qDebug("Initialized histogram with %d channels, %d resolution", channels, resolution);
}

HistogramCollection::~HistogramCollection()
{
    free(m_data);
    free(m_axisBase);
}

void HistogramCollection::resize(quint32 channels, quint32 resolution)
{
    assert(channels <= MAX_CHANNEL_COUNT);
    m_data = static_cast<double *>(realloc(m_data, channels * resolution * sizeof(double)));
    m_axisBase = static_cast<double *>(realloc(m_axisBase, resolution * sizeof(double)));
    m_channels = channels;
    m_resolution = resolution;
    initHistogram();
}

void HistogramCollection::initHistogram(void)
{
    qDebug("initializing %d x %d", m_channels, m_resolution);
    for(quint32 i = 0; i < m_channels; i++){
        for(quint32 j = 0; j< m_resolution; j++){
            m_data[i*m_resolution+j] = 0;
            m_axisBase[j] = j;
        }
        m_mean[i] = 0;
        m_sigma[i] = 0;
        m_counts[i] = 0;
        m_maxchan[i] = 0;
        m_maximum[i] = 0;
        m_overflow[i] = 0;
        m_totalCounts[i] = 0;
    }
}

void HistogramCollection::clearChan(quint32 chan)
{
    if (chan >= m_channels)
        return;

    for(quint32 i = 0; i < m_resolution; i++)
        m_data[chan * m_resolution + i] = 0;
    m_mean[chan] = 0;
    m_sigma[chan] = 0;
    m_counts[chan] = 0;
    m_maxchan[chan] = 0;
    m_maximum[chan] = 0;
    m_overflow[chan] = 0;
    m_totalCounts[chan] = 0;
}

void HistogramCollection::clearHistogram()
{
    for(quint32 i = 0; i < m_channels; i++)
        clearChan(i);
}

// calculate counts, maximum, mean and sigma for given channel range
void HistogramCollection::calcStatistics(quint32 chan, quint32 start, quint32 stop)
{
    if (chan >= m_channels)
        return;

    quint32 swap = 0;
    if(start > stop){
        stop = swap;
        stop = start;
        start = swap;
    }

    start = qMin(start, m_resolution);
    stop  = qMin(stop, m_resolution);

    double dval = 0;
    m_mean[chan] = 0;
    m_counts[chan] = 0;
    m_sigma[chan] = 0;
    m_maximum[chan] = 0;
    m_maxchan[chan] = 0;

    // calc mean and total counts
    for(quint32 i=start; i<stop; i++){
        m_mean[chan] += m_data[chan*m_resolution + i]*i;
        m_counts[chan] += m_data[chan*m_resolution + i];
        if(m_data[chan*m_resolution + i] > m_maximum[chan]){
            m_maxchan[chan] = i;
            m_maximum[chan] = m_data[chan*m_resolution + i];
        }
    }
//    qDebug("counts: %d",(quint32)m_counts[chan]);
    if(m_counts[chan])
        m_mean[chan] /= m_counts[chan];
    else
        m_mean[chan] = 0;

    if(m_mean[chan]){
        // calc sigma
        for(quint32 i=start; i<stop; i++){
            swap = m_data[chan*m_resolution + i];
            if(swap){
                dval = i - m_mean[chan];
                dval *= dval;
                m_sigma[chan] += dval * swap;
            }
        }
    }
    m_sigma[chan] = sqrt(m_sigma[chan]/m_counts[chan]);
}


double HistogramCollection::getValue(quint32 channelIndex, quint32 valueIndex)
{

    if(valueIndex < m_resolution && channelIndex < m_channels)
    {
        return m_data[channelIndex * m_resolution + valueIndex];
    }

    return 0;
}


bool HistogramCollection::incValue(quint32 channelIndex, quint32 valueIndex)
{
    //qDebug() << "channelIndex =" << channelIndex << ", value =" << valueIndex
    //    << "m_channels =" << m_channels
    //    << "m_resolution =" << m_resolution;

    if (channelIndex < m_channels)
    {
        if (valueIndex >= m_resolution)
        {
            ++m_overflow[channelIndex];
            return false;
        }
        else
        {
            ++m_data[channelIndex * m_resolution + valueIndex];
            return true;
        }
        ++m_totalCounts[channelIndex];
    }
    return false;
}

quint64 HistogramCollection::getMaxTotalCount() const
{
    quint64 ret = 0;

    for (quint32 chan=0; chan<m_channels; ++chan)
    {
        ret = std::max(ret, m_totalCounts[chan]);
    }

    return ret;
}


#if 0
QTextStream &writeHistogramCollection(QTextStream &out, HistogramCollection *histo)
{
    out << "channels: " << histo->m_channels
        << " resolution: " << histo->m_resolution
        << endl;

    for (quint32 valueIndex=0; valueIndex<histo->m_resolution; ++valueIndex)
    {
        out << histo->m_axisBase[valueIndex] << " ";
        for (quint32 channelIndex=0; channelIndex < histo->m_channels; ++channelIndex)
        {
            out << histo->m_data[channelIndex * histo->m_resolution + valueIndex] << " ";
        }
        out << endl;
    }

    return out;
}

QTextStream &readHistogramCollectionInto(QTextStream &in, HistogramCollection *histo)
{
    quint32 channels = 0;
    quint32 resolution = 0;
    QString buffer;

    in >> buffer >> channels >> buffer >> resolution;

    if (in.status() == QTextStream::Ok && channels && resolution)
    {
        histo->resize(channels, resolution);
        for (quint32 valueIndex=0; valueIndex<resolution; ++valueIndex)
        {
            double axisBaseValue;
            in >> axisBaseValue;
            histo->setAxisBaseValue(valueIndex, axisBaseValue);

            for (quint32 channelIndex=0; channelIndex < channels; ++channelIndex)
            {
                double value;
                in >> value;
                histo->setValue(channelIndex, valueIndex, value);
            }
        }
    }

    return in;
}
#endif

QTextStream &writeHistogram(QTextStream &out, HistogramCollection *histo, quint32 channelIndex)
{
    if (channelIndex < histo->m_channels)
    {
        out << "channel: " << channelIndex << endl;
        for (quint32 valueIndex=0; valueIndex<histo->m_resolution; ++valueIndex)
        {
            out << histo->m_axisBase[valueIndex] << " "
                << histo->getValue(channelIndex, valueIndex)
                << endl;
        }
    }

    return out;
}

#if 0
QTextStream &readHistogram(QTextStream &in, HistogramCollection *histo, quint32 *channelIndexOut)
{
    quint32 channelIndex;
    QString buffer;

    in >> buffer >> channelIndex;

    while (in.status() == QTextStream::Ok)
    {
        if (channelIndexOut)
        {
            *channelIndexOut = channelIndex;
        }

        quint32 valueIndex = 0;
        double value = 0;

        in >> valueIndex >> value;

        if (in.status() == QTextStream::Ok)
        {
            histo->setValue(channelIndex, valueIndex, value);
        }
    }

    return in;
}
#endif
