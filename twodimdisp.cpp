#include "twodimdisp.h"

#include <QMdiSubWindow>
#include <qwt_scale_engine.h>
#include <qwt_plot_zoomer.h>
#include <QString>

#include "qwt_plot_curve.h"

#include "twodimwidget.h"
#include "ui_twodimwidget.h"
#include "histogram.h"
#include "mvme.h"
#include "scrollzoomer.h"

TwoDimDisp::TwoDimDisp(QWidget *parent) :
    QMdiSubWindow(parent)
{
    myWidget = new TwoDimWidget(this);
    this->setWidget(myWidget);
    m_currentChannel = 0;
    m_currentModule = 0;
    curve = new QwtPlotCurve();
    curve->setStyle(QwtPlotCurve::Steps);
    //curve->setBaseline(1.0);


    curve->attach(myWidget->ui->mainPlot);
    myWidget->ui->mainPlot->replot();
}

TwoDimDisp::~TwoDimDisp()
{
    myWidget->~TwoDimWidget();
}


void TwoDimDisp::plot()
{
    QString str;
    curve->setRawSamples((const double*)m_pMyHist->m_axisBase, (const double*)m_pMyHist->m_data + 8192*m_currentChannel, 8192);
//    myWidget->ui->mainPlot->setAxisScale( QwtPlot::yLeft, -200.0, 200.0 );
    //myWidget->ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft, false);

    myWidget->setZoombase();
    m_pMyHist->calcStatistics(m_currentChannel, myWidget->m_myZoomer->getLowborder(), myWidget->m_myZoomer->getHiborder());
    myWidget->ui->mainPlot->replot();
    str.sprintf("%2.2f", m_pMyHist->m_mean[m_currentChannel]);
    myWidget->ui->meanval->setText(str);
    str.sprintf("%2.2f", m_pMyHist->m_sigma[m_currentChannel]);
    myWidget->ui->sigmaval->setText(str);
    str.sprintf("%2.2f", m_pMyHist->m_counts[m_currentChannel]);
    myWidget->ui->countval->setText(str);
    str.sprintf("%d", (quint32)m_pMyHist->m_counts[m_currentChannel]);
    myWidget->ui->countval->setText(str);
    str.sprintf("%d", (quint32) m_pMyHist->m_maximum[m_currentChannel]);
    myWidget->ui->maxval->setText(str);
    str.sprintf("%d", (quint32) m_pMyHist->m_maxchan[m_currentChannel]);
    myWidget->ui->maxpos->setText(str);
}

void TwoDimDisp::setMvme(mvme *m)
{
    m_pMyMvme = m;
}

void TwoDimDisp::setHistogram(Histogram *h)
{
    m_pMyHist = h;
}

void TwoDimDisp::displayChanged()
{
    if(myWidget->ui->moduleBox->value() != m_currentModule){
        m_currentModule = myWidget->ui->moduleBox->value();
        m_pMyHist = m_pMyMvme->getHist(m_currentModule);
    }
    if(myWidget->ui->channelBox->value() != m_currentChannel){
        m_currentChannel = myWidget->ui->channelBox->value();
    }
//    qDebug("current mod %d channel %d", m_currentModule, m_currentChannel);
    plot();
}

void TwoDimDisp::clearDisp()
{
    m_pMyHist->clearChan(m_currentChannel);
}



