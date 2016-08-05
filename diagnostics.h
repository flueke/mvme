#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <QObject>
#include "mvme.h"

#ifdef MAX
#undef MAX
#endif

#ifdef MIN
#undef MIN
#endif

#define MAX 40
#define MIN 41
#define ODD 42
#define EVEN 43
#define MAXFILT 44
#define MINFILT 45
#define ODDFILT 46
#define EVENFILT 47

class Histogram;

class Diagnostics : public QObject
{
    Q_OBJECT
public:
    explicit Diagnostics(QObject *parent = 0);
    void clear(void);
    void calcAll(Histogram *p_myHist, quint16 lo, quint16 hi, quint16 lo2, quint16 hi2, quint16 binLo, quint16 binHi);
    double getMean(quint16 chan);
    double getSigma(quint16 chan);
    quint32 getMeanchannel(quint16 chan);
    quint32 getSigmachannel(quint16 chan);
    quint32 getMax(quint16 chan);
    quint32 getMaxchan(quint16 chan);
    quint32 getCounts(quint16 chan);
    quint32 getChannel(Histogram *p_myHist, quint16 chan, quint32 bin);

signals:
    
public slots:
    
private:
    double mean[50];
    double sigma[50];
    quint32 meanchannel[50];
    quint32 sigmachannel[50];
    quint32 max[50];
    quint32 maxchan[50];
    quint32 counts[50];
};

#endif // DIAGNOSTICS_H
