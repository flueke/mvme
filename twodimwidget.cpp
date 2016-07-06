#include "twodimwidget.h"
#include "ui_twodimwidget.h"
#include "twodimdisp.h"
#include "qwt_plot_zoomer.h"
#include <qwt_scale_engine.h>
#include <qwt_plot_renderer.h>
#include <qwt_scale_widget.h>
#include "scrollzoomer.h"
#include <QDebug>

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

    // assign the unused rRight axis to only zoom in x
    m_myZoomer->setAxis(QwtPlot::xBottom, QwtPlot::yRight);
    m_myZoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_myZoomer->setZoomBase();

    qDebug() << "zoomBase =" << m_myZoomer->zoomBase();

    connect(m_myZoomer, SIGNAL(zoomed(QRectF)),
            this, SLOT(zoomerZoomed(QRectF)));

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

    ui->mainPlot->axisWidget(QwtPlot::xBottom)->setTitle(
                QString("Channel %1").arg(getSelectedChannelIndex()));

    m_pMyDisp->displayChanged();
}

void TwoDimWidget::clearHist()
{
    m_pMyDisp->clearDisp();
    m_pMyDisp->plot();
}


void TwoDimWidget::setZoombase()
{
    m_myZoomer->setZoomBase();
    qDebug() << "zoomBase =" << m_myZoomer->zoomBase();
}

void TwoDimWidget::zoomerZoomed(QRectF zoomRect)
{
    if (m_myZoomer->zoomRectIndex() == 0)
    {
        ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft);
        ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, 8192);
        ui->mainPlot->replot();
        m_myZoomer->setZoomBase();
    }
    m_pMyDisp->updateStatistics();
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
