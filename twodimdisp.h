#ifndef TWODIMDISP_H
#define TWODIMDISP_H

#include <QWidget>
#include <QtGui>
#include <QMdiSubWindow>

#include "twodimwidget.h"

class QwtPlotCurve;
class Histogram;
class mvme;
class QwtPlotTextLabel;
class QwtText;

namespace Ui{
    class TwoDimDisp;
}

class TwoDimDisp : public QMdiSubWindow
{
    Q_OBJECT

public:
    TwoDimDisp(QWidget *parent = 0);
    ~TwoDimDisp();
    void plot();
    void setMvme(mvme* m);
    void setHistogram(Histogram* h);
    void displayChanged(void);
    void clearDisp(void);
    void updateStatistics();

    QwtPlotCurve *curve;

private:
    TwoDimWidget *myWidget;

    Histogram* m_pMyHist;
    quint32 m_currentModule;
    quint32 m_currentChannel;
    mvme* m_pMyMvme;

    QwtPlotTextLabel *m_statsTextItem;
    QwtText *m_statsText;

signals:
    void modChanged(qint16 mod);
};

#endif // TWODIMDISP_H
