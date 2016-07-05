#include "twodimdisp.h"

#include <QMdiSubWindow>
#include <qwt_scale_engine.h>
#include <qwt_plot_zoomer.h>
#include <QString>
#include <QDebug>

#include "qwt_plot_curve.h"
#include <qwt_plot_textlabel.h>
#include <qwt_text.h>

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


    m_statsText = new QwtText();
    m_statsText->setRenderFlags(Qt::AlignLeft | Qt::AlignTop);

    m_statsTextItem = new QwtPlotTextLabel;
    m_statsTextItem->setText(*m_statsText);
    m_statsTextItem->attach(myWidget->ui->mainPlot);

    myWidget->ui->mainPlot->replot();
}

TwoDimDisp::~TwoDimDisp()
{
    qDebug() << __PRETTY_FUNCTION__;
    myWidget->~TwoDimWidget();
}


void TwoDimDisp::plot()
{
    //qDebug() << __PRETTY_FUNCTION__;

    curve->setRawSamples((const double*)m_pMyHist->m_axisBase,
        (const double*)m_pMyHist->m_data + m_pMyHist->m_resolution*m_currentChannel,
                         m_pMyHist->m_resolution);
//    myWidget->ui->mainPlot->setAxisScale( QwtPlot::yLeft, -200.0, 200.0 );
    //myWidget->ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft, false);

    //myWidget->setZoombase();
    updateStatistics();
}

void TwoDimDisp::updateStatistics()
{
    m_pMyHist->calcStatistics(m_currentChannel, myWidget->m_myZoomer->getLowborder(), myWidget->m_myZoomer->getHiborder());


    //myWidget->ui->mainPlot->replot();

    QString str;
    str.sprintf("%2.2f", m_pMyHist->m_mean[m_currentChannel]);
    myWidget->ui->meanval->setText(str);

    str.sprintf("%2.2f", m_pMyHist->m_sigma[m_currentChannel]);
    myWidget->ui->sigmaval->setText(str);

    str.sprintf("%d", (quint32)m_pMyHist->m_counts[m_currentChannel]);
    myWidget->ui->countval->setText(str);

    str.sprintf("%d", (quint32) m_pMyHist->m_maximum[m_currentChannel]);
    myWidget->ui->maxval->setText(str);

    str.sprintf("%d", (quint32) m_pMyHist->m_maxchan[m_currentChannel]);
    myWidget->ui->maxpos->setText(str);

    m_statsText->setText(
                QString::asprintf("\nMean: %2.2f\nSigma: %2.2f\nCounts: %u\nMaximum: %u\nat Channel: %u",
                                           m_pMyHist->m_mean[m_currentChannel],
                                           m_pMyHist->m_sigma[m_currentChannel],
                                           (quint32)m_pMyHist->m_counts[m_currentChannel],
                                           (quint32)m_pMyHist->m_maximum[m_currentChannel],
                                           (quint32)m_pMyHist->m_maxchan[m_currentChannel]
                                           ));
    m_statsTextItem->setText(*m_statsText);
    curve->plot()->replot();
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
    if((quint32)myWidget->ui->moduleBox->value() != m_currentModule){
        m_currentModule = myWidget->ui->moduleBox->value();
        m_pMyHist = m_pMyMvme->getHist(m_currentModule);
    }
    if((quint32)myWidget->ui->channelBox->value() != m_currentChannel){
        m_currentChannel = myWidget->ui->channelBox->value();
        m_currentChannel = qMin(m_currentChannel, m_pMyHist->m_channels - 1);
        QSignalBlocker sb(myWidget->ui->channelBox);
        myWidget->ui->channelBox->setValue(m_currentChannel);
    }
//    qDebug("current mod %d channel %d", m_currentModule, m_currentChannel);
    plot();
}

void TwoDimDisp::clearDisp()
{
    m_pMyHist->clearChan(m_currentChannel);
}



