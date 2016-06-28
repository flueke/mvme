#include "twodimwidget.h"
#include "ui_twodimwidget.h"
#include "twodimdisp.h"
#include "qwt_plot_zoomer.h"
#include <qwt_scale_engine.h>
#include "scrollzoomer.h"

TwoDimWidget::TwoDimWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TwoDimWidget)
{
    ui->setupUi(this);
    m_pMyDisp = (TwoDimDisp*) parent;
//    ui->mainPlot->setAxisAutoScale(QwtPlot::xBottom, true);
    ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, 8192);
    m_myZoomer = new ScrollZoomer(this->ui->mainPlot->canvas());
    //m_myZoomer = new ScrollZoomer(0);
    m_myZoomer->setZoomBase();

    QwtLogScaleEngine* logSe = new QwtLogScaleEngine;
    logSe->setAttribute(QwtScaleEngine::Inverted, false);

    QwtLinearScaleEngine* linSe = new QwtLinearScaleEngine;

    //ui->mainPlot->setAxisScaleEngine(QwtPlot::yLeft, logSe);
    //ui->mainPlot->setAxisScale(QwtPlot::yLeft, 1.0, 1e9);
}

TwoDimWidget::~TwoDimWidget()
{
    delete ui;
}

void TwoDimWidget::displaychanged()
{
    qDebug("display changed");
    m_pMyDisp->displayChanged();
}

void TwoDimWidget::clearHist()
{
    m_pMyDisp->clearDisp();
}


void TwoDimWidget::setZoombase()
{
    m_myZoomer->setZoomBase();
}
