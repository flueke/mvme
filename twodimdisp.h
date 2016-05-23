#ifndef TWODIMDISP_H
#define TWODIMDISP_H

#include <QWidget>
#include <QtGui>
#include <QMdiSubWindow>

#include "twodimwidget.h"

class QwtPlotCurve;
class Histogram;
class mvme;

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

private:
    TwoDimWidget *myWidget;
    QwtPlotCurve *curve;
    Histogram* m_pMyHist;
    quint16 m_currentModule;
    quint16 m_currentChannel;
    mvme* m_pMyMvme;

signals:
    void modChanged(qint16 mod);
};

#endif // TWODIMDISP_H
