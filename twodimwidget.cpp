#include "twodimwidget.h"
#include "ui_twodimwidget.h"
#include "twodimdisp.h"
#include "qwt_plot_zoomer.h"
#include <qwt_scale_engine.h>
#include <qwt_plot_renderer.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_curve.h>
#include "scrollzoomer.h"
#include <QDebug>

TwoDimWidget::TwoDimWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TwoDimWidget)
{
    ui->setupUi(this);

    m_pMyDisp = (TwoDimDisp*) parent;

    // TODO: make this depend on the histograms resolution
    ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, 8192);

    ui->mainPlot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");
    ui->mainPlot->axisWidget(QwtPlot::xBottom)->setTitle("Channel 0");

    m_myZoomer = new ScrollZoomer(this->ui->mainPlot->canvas());
    // assign the unused rRight axis to only zoom in x
    m_myZoomer->setAxis(QwtPlot::xBottom, QwtPlot::yRight);
    m_myZoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_myZoomer->setZoomBase();

    connect(m_myZoomer, SIGNAL(zoomed(QRectF)),
            this, SLOT(zoomerZoomed(QRectF)));

    qDebug() << "zoomBase =" << m_myZoomer->zoomBase();

    m_plotPanner = new QwtPlotPanner(ui->mainPlot->canvas());
    m_plotPanner->setAxisEnabled(QwtPlot::yLeft, false);
    m_plotPanner->setMouseButton(Qt::MiddleButton);

    auto plotMagnifier = new QwtPlotMagnifier(ui->mainPlot->canvas());
    plotMagnifier->setAxisEnabled(QwtPlot::yLeft, false);
    plotMagnifier->setMouseButton(Qt::NoButton);
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

    if (ui->dispLin->isChecked() &&
            !dynamic_cast<QwtLinearScaleEngine *>(ui->mainPlot->axisScaleEngine(QwtPlot::yLeft)))
    {
        m_pMyDisp->curve->setBaseline(0.0);
        ui->mainPlot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (ui->dispLog->isChecked() &&
             !dynamic_cast<QwtLogScaleEngine *>(ui->mainPlot->axisScaleEngine(QwtPlot::yLeft)))
    {
        // TODO(flueke): this does not work properly

        m_pMyDisp->curve->setBaseline(0.1);
        ui->mainPlot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLogScaleEngine);
        ui->mainPlot->axisScaleEngine(QwtPlot::yLeft)->setAttribute(QwtScaleEngine::Floating);
        //ui->mainPlot->setAxisScale(QwtPlot::yLeft, 0.1, 6000.0);

        //ui->mainPlot->setAxisMaxMajor(QwtPlot::yLeft, 10);
        //ui->mainPlot->setAxisMaxMinor(QwtPlot::yLeft, 10);
    }

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
