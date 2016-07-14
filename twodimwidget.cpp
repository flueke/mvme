#include "twodimwidget.h"
#include "ui_twodimwidget.h"
#include "qwt_plot_zoomer.h"
#include <qwt_scale_engine.h>
#include <qwt_plot_renderer.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_textlabel.h>
#include <qwt_text.h>
//#include <QSignalBlocker> // Qt-5.2 does not have this yet
#include "scrollzoomer.h"
#include <QDebug>
#include "histogram.h"
#include "mvme.h"

TwoDimWidget::TwoDimWidget(mvme *context, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TwoDimWidget)
    , m_curve(new QwtPlotCurve)
    , m_pMyHist(0)
    , m_currentModule(0)
    , m_currentChannel(0)
    , m_pMyMvme(context)
{
    ui->setupUi(this);

    m_curve->setStyle(QwtPlotCurve::Steps);
    m_curve->attach(ui->mainPlot);

    // TODO: make this depend on the histograms resolution
    ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, 8192);

    ui->mainPlot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");
    ui->mainPlot->axisWidget(QwtPlot::xBottom)->setTitle("Channel 0");

    m_plotZoomer = new ScrollZoomer(this->ui->mainPlot->canvas());
    // assign the unused rRight axis to only zoom in x
    m_plotZoomer->setAxis(QwtPlot::xBottom, QwtPlot::yRight);
    m_plotZoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_plotZoomer->setZoomBase();

    connect(m_plotZoomer, SIGNAL(zoomed(QRectF)),
            this, SLOT(zoomerZoomed(QRectF)));

    qDebug() << "zoomBase =" << m_plotZoomer->zoomBase();

    auto plotPanner = new QwtPlotPanner(ui->mainPlot->canvas());
    plotPanner->setAxisEnabled(QwtPlot::yLeft, false);
    plotPanner->setMouseButton(Qt::MiddleButton);

    auto plotMagnifier = new QwtPlotMagnifier(ui->mainPlot->canvas());
    plotMagnifier->setAxisEnabled(QwtPlot::yLeft, false);
    plotMagnifier->setMouseButton(Qt::NoButton);

    m_statsText = new QwtText();
    m_statsText->setRenderFlags(Qt::AlignLeft | Qt::AlignTop);

    m_statsTextItem = new QwtPlotTextLabel;
    m_statsTextItem->setText(*m_statsText);
    m_statsTextItem->attach(ui->mainPlot);

    ui->mainPlot->replot();
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
        m_curve->setBaseline(0.0);
        ui->mainPlot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (ui->dispLog->isChecked() &&
             !dynamic_cast<QwtLogScaleEngine *>(ui->mainPlot->axisScaleEngine(QwtPlot::yLeft)))
    {
        // TODO(flueke): this does not work properly

        m_curve->setBaseline(0.1);
        ui->mainPlot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLogScaleEngine);
        ui->mainPlot->axisScaleEngine(QwtPlot::yLeft)->setAttribute(QwtScaleEngine::Floating);
        //ui->mainPlot->setAxisScale(QwtPlot::yLeft, 0.1, 6000.0);

        //ui->mainPlot->setAxisMaxMajor(QwtPlot::yLeft, 10);
        //ui->mainPlot->setAxisMaxMinor(QwtPlot::yLeft, 10);
    }

    if((quint32)ui->moduleBox->value() != m_currentModule)
    {
        m_currentModule = ui->moduleBox->value();
        m_pMyHist = m_pMyMvme->getHist(m_currentModule);
    }

    if((quint32)ui->channelBox->value() != m_currentChannel)
    {
        m_currentChannel = ui->channelBox->value();
        m_currentChannel = qMin(m_currentChannel, m_pMyHist->m_channels - 1);
        //QSignalBlocker sb(ui->channelBox);
	ui->channelBox->blockSignals(true);
        ui->channelBox->setValue(m_currentChannel);
	ui->channelBox->blockSignals(false);
    }

    plot();
}

void TwoDimWidget::clearHist()
{
    clearDisp();
    plot();
}


void TwoDimWidget::setZoombase()
{
    m_plotZoomer->setZoomBase();
    qDebug() << "zoomBase =" << m_plotZoomer->zoomBase();
}

void TwoDimWidget::zoomerZoomed(QRectF zoomRect)
{
    if (m_plotZoomer->zoomRectIndex() == 0)
    {
        ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft);
        ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, 8192);
        ui->mainPlot->replot();
        m_plotZoomer->setZoomBase();
    }
    updateStatistics();
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
    QString fileName;
    fileName.sprintf("histogram_channel%02u.pdf", getSelectedChannelIndex());

    QwtPlotRenderer renderer;
    renderer.exportTo(ui->mainPlot, fileName);
}

void TwoDimWidget::plot()
{
    m_curve->setRawSamples((const double*)m_pMyHist->m_axisBase,
        (const double*)m_pMyHist->m_data + m_pMyHist->m_resolution*m_currentChannel,
                         m_pMyHist->m_resolution);

    updateStatistics();
    m_curve->plot()->replot();
}

void TwoDimWidget::updateStatistics()
{
    m_pMyHist->calcStatistics(m_currentChannel, m_plotZoomer->getLowborder(), m_plotZoomer->getHiborder());

    QString str;
    str.sprintf("%2.2f", m_pMyHist->m_mean[m_currentChannel]);
    ui->meanval->setText(str);

    str.sprintf("%2.2f", m_pMyHist->m_sigma[m_currentChannel]);
    ui->sigmaval->setText(str);

    str.sprintf("%d", (quint32)m_pMyHist->m_counts[m_currentChannel]);
    ui->countval->setText(str);

    str.sprintf("%d", (quint32) m_pMyHist->m_maximum[m_currentChannel]);
    ui->maxval->setText(str);

    str.sprintf("%d", (quint32) m_pMyHist->m_maxchan[m_currentChannel]);
    ui->maxpos->setText(str);

    QString buffer;
    buffer.sprintf("\nMean: %2.2f\nSigma: %2.2f\nCounts: %u\nMaximum: %u\nat Channel: %u",
                               m_pMyHist->m_mean[m_currentChannel],
                               m_pMyHist->m_sigma[m_currentChannel],
                               (quint32)m_pMyHist->m_counts[m_currentChannel],
                               (quint32)m_pMyHist->m_maximum[m_currentChannel],
                               (quint32)m_pMyHist->m_maxchan[m_currentChannel]
                               );

    m_statsText->setText(buffer);
    m_statsTextItem->setText(*m_statsText);
}

void TwoDimWidget::setMvme(mvme *m)
{
    m_pMyMvme = m;
}

void TwoDimWidget::setHistogram(Histogram *h)
{
    m_pMyHist = h;
}

void TwoDimWidget::clearDisp()
{
    m_pMyHist->clearChan(m_currentChannel);
}
