#include "diagnostics.h"
#include "histogram.h"
#include "math.h"

Diagnostics::Diagnostics(QObject *parent) :
    QObject(parent)
{
    p_myMvme = (mvme*)parent;

}

void Diagnostics::clear()
{
    quint16 i;

    mean[0] = 0;
    for(i=0;i<50;i++){
        mean[i] = 0;
        sigma[i] = 0;
        meanchannel[i] = 0;
        sigmachannel[i] = 0;
        max[i] = 0;
        maxchan[i] = 0;
        counts[i] = 0;
    }
    // set minima to hi values
    mean[MIN] = 128000;
    sigma[MIN] = 128000;
    mean[MINFILT] = 128000;
    sigma[MINFILT] = 128000;
}

void Diagnostics::calcAll(Histogram* p_myHist, quint16 lo, quint16 hi, quint16 lo2, quint16 hi2, quint16 binLo, quint16 binHi)
{
    quint16 i, j;
    quint16 evencounts = 0, evencounts2 = 0, oddcounts = 0, oddcounts2 = 0;
    double dval;
    quint32 res = p_myHist->m_resolution;
    qDebug("%d %d", binLo, binHi);
    //reset all old calculations
    clear();

    // iterate through all channels (34 real channels max.)
    for(i=0; i<34; i++){
        // calculate means and maxima
        for(j=binLo; j<=binHi; j++){
            mean[i] += p_myHist->m_data[i*res + j]*j;
            counts[i] += p_myHist->m_data[i*res + j];
//            if(i<2 && p_myHist->m_data[i*res + j] > 0)
//                qDebug("%d %d %2.2f", j, res, p_myHist->m_data[i*res+j]);
            if(p_myHist->m_data[i*res + j] > max[i]){
                maxchan[i] = j;
                max[i] = p_myHist->m_data[i*res + j];
            }
        }
        if(counts[i])
            mean[i] /= counts[i];
        else
            mean[i] = 0;

        if(mean[i]){
            // calculate sigmas
            for(j=binLo; j<=binHi; j++){
                dval =  j - mean[i];
                dval *= dval;
                dval *= p_myHist->m_data[i*res + j];
                sigma[i] += dval;
            }
            if(counts[i])
                sigma[i] = sqrt(sigma[i]/counts[i]);
            else
                sigma[i] = 0;
        }
    }

    // find max and min mean and sigma
    for(i=0; i<34; i++){
        if(i>=lo && i <= hi){
            if(mean[i] > mean[MAX]){
                mean[MAX] = mean[i];
                meanchannel[MAX] = i;
            }
            if(mean[i] < mean[MIN]){
                mean[MIN] = mean[i];
                meanchannel[MIN] = i;
            }
            if(sigma[i] > sigma[MAX]){
                sigma[MAX] = sigma[i];
                sigmachannel[MAX] = i;
            }
            if(sigma[i] < sigma[MIN]){
                sigma[MIN] = sigma[i];
                sigmachannel[MIN] = i;
            }
        }
        if(i>=lo2 && i <= hi2){
            if(mean[i] > mean[MAXFILT]){
                mean[MAXFILT] = mean[i];
                meanchannel[MAXFILT] = i;
            }
            if(mean[i] < mean[MINFILT]){
                mean[MINFILT] = mean[i];
                meanchannel[MINFILT] = i;
            }
            if(sigma[i] > sigma[MAXFILT]){
                sigma[MAXFILT] = sigma[i];
                sigmachannel[MAXFILT] = i;
            }
            if(sigma[i] < sigma[MINFILT]){
                sigma[MINFILT] = sigma[i];
                sigmachannel[MINFILT] = i;
            }
        }
    }

    // now odds and evens
    for(i=0; i<34; i++){
        // calculate means and maxima
        // odd?
        if(i%2){
            if(i>=lo && i <= hi){
                mean[ODD] += mean[i];
                counts[ODD] += counts[i];
                oddcounts++;
            }
            if(i>=lo2 && i <= hi2){
                mean[ODDFILT] += mean[i];
                counts[ODDFILT] += counts[i];
                oddcounts2++;
            }
        }
        else{
            if(i>=lo && i <= hi){
                mean[EVEN] += mean[i];
                counts[EVEN] += counts[i];
                evencounts++;
            }
            if(i>=lo2 && i <= hi2){
                mean[EVENFILT] += mean[i];
                counts[EVENFILT] += counts[i];
                evencounts2++;
            }
        }
    }
    mean[EVEN] /= evencounts;
    mean[ODD] /= oddcounts;
    mean[EVENFILT] /= evencounts2;
    mean[ODDFILT] /= oddcounts2;

    // calculate sigmas
    for(i=0; i<34; i++){
        for(j=binLo; j<=binHi; j++){
            dval =  j - mean[i];
            dval *= dval,
            dval *= p_myHist->m_data[i*res + j];
            if(i%2){
                if(i>=lo && i <= hi)
                    sigma[ODD] += dval;
                if(i>=lo2 && i <= hi2)
                    sigma[ODDFILT] += dval;
            }
            else{
                if(i>=lo && i <= hi)
                    sigma[EVEN] += dval;
                if(i>=lo2 && i <= hi2)
                    sigma[EVENFILT] += dval;
            }
        }
    }
//    qDebug("%2.2f, %2.2f, %2.2f, %2.2f, %2.2f, %2.2f", counts[EVEN], sigma[EVEN], counts[ODD], sigma[ODD], sigma[32], sigma[33]);

    if(counts[EVEN])
        sigma[EVEN] = sqrt(sigma[EVEN]/counts[EVEN]);
    else
        sigma[EVEN] = 0;

    if(counts[ODD])
        sigma[ODD] = sqrt(sigma[ODD]/counts[ODD]);
    else
        sigma[ODD] = 0;

    if(counts[EVENFILT])
        sigma[EVENFILT] = sqrt(sigma[EVENFILT]/counts[EVENFILT]);
    else
        sigma[EVENFILT] = 0;

    if(counts[ODDFILT])
        sigma[ODDFILT] = sqrt(sigma[ODDFILT]/counts[ODDFILT]);
    else
        sigma[ODDFILT] = 0;

}


double Diagnostics::getMean(quint16 chan)
{
    return mean[chan];
}

double Diagnostics::getSigma(quint16 chan)
{
    return sigma[chan];
}

quint32 Diagnostics::getMeanchannel(quint16 chan)
{
    return meanchannel[chan];
}

quint32 Diagnostics::getSigmachannel(quint16 chan)
{
    return sigmachannel[chan];
}

quint32 Diagnostics::getMax(quint16 chan)
{
    return max[chan];
}

quint32 Diagnostics::getMaxchan(quint16 chan)
{
    return maxchan[chan];
}

quint32 Diagnostics::getCounts(quint16 chan)
{
    return counts[chan];
}

quint32 Diagnostics::getChannel(Histogram* p_myHist, quint16 chan, quint32 bin)
{
    quint32 res = p_myHist->m_resolution;
    return p_myHist->m_data[chan*res + bin];
}
