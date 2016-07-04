#include "twodimwidget.h"
#include "ui_twodimwidget.h"
#include "twodimdisp.h"
#include "qwt_plot_zoomer.h"
#include <qwt_scale_engine.h>
#include <qwt_plot_renderer.h>
#include <qwt_scale_widget.h>
#include "scrollzoomer.h"

TwoDimWidget::TwoDimWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TwoDimWidget)
{
    ui->setupUi(this);
    m_pMyDisp = (TwoDimDisp*) parent;
//    ui->mainPlot->setAxisAutoScale(QwtPlot::xBottom, true);
    ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, 8192);

    ui->mainPlot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");
    ui->mainPlot->axisWidget(QwtPlot::xBottom)->setTitle("Channel 0");

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

    ui->mainPlot->axisWidget(QwtPlot::xBottom)->setTitle(
                QString("Channel %1").arg(getSelectedChannelIndex()));
}

void TwoDimWidget::clearHist()
{
    m_pMyDisp->clearDisp();
    m_pMyDisp->plot();
}


void TwoDimWidget::setZoombase()
{
    m_myZoomer->setZoomBase();
}

quint32 TwoDimWidget::getSelectedChannelIndex() const
{
    return static_cast<quint32>(ui->channelBox->value());
}

void TwoDimWidget::setSelectedChannelIndex(quint32 channelIndex)
{
    ui->channelBox->setValue(channelIndex);
}

void TwoDimWidget::exportPlot()
{
    QString fileName = QString::asprintf("histogram_channel%02u.pdf", getSelectedChannelIndex());

    QwtPlotRenderer renderer;
    renderer.exportTo(ui->mainPlot, fileName);
}
