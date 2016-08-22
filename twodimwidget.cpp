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
#include "scrollzoomer.h"
#include <QDebug>
#include "histogram.h"
#include "mvme.h"
#include "mvme_context.h"

// Bounds values to 0.1 to make QwtLogScaleEngine happy
class MinBoundLogTransform: public QwtLogTransform
{
    public:
        virtual double bounded(double value) const
        {
            double result = qBound(0.1, value, QwtLogTransform::LogMax);
            return result;
        }

        virtual double transform(double value) const
        {
            double result = QwtLogTransform::transform(bounded(value));
            return result;
        }

        virtual double invTransform(double value) const
        {
            double result = QwtLogTransform::invTransform(value);
            return result;
        }

        virtual QwtTransform *copy() const
        {
            return new MinBoundLogTransform;
        }
};

TwoDimWidget::TwoDimWidget(MVMEContext *context, Histogram *histo, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TwoDimWidget)
    , m_context(context)
    , m_curve(new QwtPlotCurve)
    , m_pMyHist(histo)
    , m_currentModule(0)
    , m_currentChannel(0)
{
    ui->setupUi(this);

    m_curve->attach(ui->mainPlot);
    m_curve->setStyle(QwtPlotCurve::Steps);
    m_curve->setCurveAttribute(QwtPlotCurve::Inverted);

    ui->moduleBox->setEnabled(false);

    ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, m_pMyHist->m_resolution);

    ui->mainPlot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");
    ui->mainPlot->axisWidget(QwtPlot::xBottom)->setTitle("Channel 0");

    ui->mainPlot->canvas()->setMouseTracking(true);
    m_plotZoomer = new ScrollZoomer(this->ui->mainPlot->canvas());
    // assign the unused rRight axis to only zoom in x
    m_plotZoomer->setAxis(QwtPlot::xBottom, QwtPlot::yRight);
    m_plotZoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_plotZoomer->setZoomBase();

    connect(m_plotZoomer, SIGNAL(zoomed(QRectF)),this, SLOT(zoomerZoomed(QRectF)));
    connect(m_plotZoomer, SIGNAL(mouseCursorMovedTo(QPointF)), this, SLOT(mouseCursorMovedToPlotCoord(QPointF)));

    qDebug() << "zoomBase =" << m_plotZoomer->zoomBase();

#if 0
    auto plotPanner = new QwtPlotPanner(ui->mainPlot->canvas());
    plotPanner->setAxisEnabled(QwtPlot::yLeft, false);
    plotPanner->setMouseButton(Qt::MiddleButton);
#endif

    auto plotMagnifier = new QwtPlotMagnifier(ui->mainPlot->canvas());
    plotMagnifier->setAxisEnabled(QwtPlot::yLeft, false);
    plotMagnifier->setMouseButton(Qt::NoButton);

    m_statsText = new QwtText();
    m_statsText->setRenderFlags(Qt::AlignLeft | Qt::AlignTop);

    m_statsTextItem = new QwtPlotTextLabel;
    m_statsTextItem->setText(*m_statsText);
    m_statsTextItem->attach(ui->mainPlot);

    connect(context, &MVMEContext::histogramAboutToBeRemoved, this, [=](const QString &, Histogram *h) {
        if (h == histo)
        {
            auto pw = parentWidget();
            if (pw)
                pw->close();
            close();
        }
    });

#if 0
    u32 resolution = m_pMyHist->m_resolution;
    qsrand(42);

    for (u32 xIndex=0; xIndex < resolution; ++xIndex)
    {
        double yValue = 0;
        if (xIndex == 0 || xIndex == resolution-1)
        {
            yValue = 1;
        }
        else
        {
            yValue = xIndex % 16;
        }

        m_pMyHist->setValue(0, xIndex, yValue);
    }
#endif

    displayChanged();
}

TwoDimWidget::~TwoDimWidget()
{
    delete ui;
}

void TwoDimWidget::displayChanged()
{

    ui->mainPlot->axisWidget(QwtPlot::xBottom)->setTitle(
                QString("Channel %1").arg(getSelectedChannelIndex()));

    if (ui->dispLin->isChecked() && !yAxisIsLin())
    {
        ui->mainPlot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (ui->dispLog->isChecked() && !yAxisIsLog())
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        ui->mainPlot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
    }

#if 0
    if((quint32)ui->moduleBox->value() != m_currentModule)
    {
        m_currentModule = ui->moduleBox->value();
        m_pMyHist = m_pMyMvme->getHist(m_currentModule);
    }
#endif

    if((quint32)ui->channelBox->value() != m_currentChannel)
    {
        m_currentChannel = ui->channelBox->value();
        m_currentChannel = qMin(m_currentChannel, m_pMyHist->m_channels - 1);
        ui->channelBox->blockSignals(true);
        ui->channelBox->setValue(m_currentChannel);
        ui->channelBox->blockSignals(false);
    }

    auto histos = m_context->getHistograms();
    auto name = histos.key(m_pMyHist);

    setWindowTitle(QString("Histogram %1, channel=%2")
                   .arg(name)
                   .arg(m_currentChannel)
                  );

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
        if (yAxisIsLog())
        {
            updateYAxisScale();
        }
        else
        {
            ui->mainPlot->setAxisAutoScale(QwtPlot::yLeft, true);
        }

        ui->mainPlot->setAxisScale( QwtPlot::xBottom, 0, m_pMyHist->m_resolution);
        ui->mainPlot->replot();
        m_plotZoomer->setZoomBase();
    }
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
    m_curve->setRawSamples(
            (const double*)m_pMyHist->m_axisBase,
            (const double*)m_pMyHist->m_data + m_pMyHist->m_resolution*m_currentChannel,
            m_pMyHist->m_resolution);

    updateStatistics();
    updateYAxisScale();

    ui->mainPlot->replot();
}

void TwoDimWidget::updateYAxisScale()
{
    // update the y axis using the currently visible max value
    double maxValue = 1.2 * m_pMyHist->m_maximum[m_currentChannel];

    if (maxValue <= 1.0)
        maxValue = 10.0;

    double base = yAxisIsLog() ? 1.0 : 0.0l;

    ui->mainPlot->setAxisScale(QwtPlot::yLeft, base, maxValue);
}

bool TwoDimWidget::yAxisIsLog()
{
    return dynamic_cast<QwtLogScaleEngine *>(ui->mainPlot->axisScaleEngine(QwtPlot::yLeft));
}

bool TwoDimWidget::yAxisIsLin()
{
    return dynamic_cast<QwtLinearScaleEngine *>(ui->mainPlot->axisScaleEngine(QwtPlot::yLeft));
}

void TwoDimWidget::updateStatistics()
{
    auto lowerBound = qFloor(ui->mainPlot->axisScaleDiv(QwtPlot::xBottom).lowerBound());
    auto upperBound = qCeil(ui->mainPlot->axisScaleDiv(QwtPlot::xBottom).upperBound());
    //qDebug() << __PRETTY_FUNCTION__ << lowerBound << upperBound;

    m_pMyHist->calcStatistics(m_currentChannel, lowerBound, upperBound);

    QString str;
    str.sprintf("%2.2f", m_pMyHist->m_mean[m_currentChannel]);
    ui->meanval->setText(str);

    str.sprintf("%2.2f", m_pMyHist->m_sigma[m_currentChannel]);
    ui->sigmaval->setText(str);

    str.sprintf("%u", (quint32)m_pMyHist->m_counts[m_currentChannel]);
    ui->countval->setText(str);

    str.sprintf("%u", (quint32) m_pMyHist->m_maximum[m_currentChannel]);
    ui->maxval->setText(str);

    str.sprintf("%u", (quint32) m_pMyHist->m_maxchan[m_currentChannel]);
    ui->maxpos->setText(str);

    str.sprintf("%u", (quint32) m_pMyHist->m_overflow[m_currentChannel]);
    ui->overflow->setText(str);

    QString buffer;
    buffer.sprintf("\nMean: %2.2f\nSigma: %2.2f\nCounts: %u\nMaximum: %u\nat Channel: %u\nOverflow: %u",
                               m_pMyHist->m_mean[m_currentChannel],
                               m_pMyHist->m_sigma[m_currentChannel],
                               (quint32)m_pMyHist->m_counts[m_currentChannel],
                               (quint32)m_pMyHist->m_maximum[m_currentChannel],
                               (quint32)m_pMyHist->m_maxchan[m_currentChannel],
                               (quint32)m_pMyHist->m_overflow[m_currentChannel]
                               );

    m_statsText->setText(buffer);
    m_statsTextItem->setText(*m_statsText);
}

void TwoDimWidget::setHistogram(Histogram *h)
{
    m_pMyHist = h;
}

void TwoDimWidget::clearDisp()
{
    m_pMyHist->clearChan(m_currentChannel);
}

void TwoDimWidget::mouseCursorMovedToPlotCoord(QPointF point)
{
    auto xValue = static_cast<u32>(point.x());
    
    if (xValue < m_pMyHist->m_resolution)
    {
        QString str;
        str.sprintf("Channel %u\nCounts %u", xValue,
                static_cast<u32>(m_pMyHist->getValue(m_currentChannel, xValue)));
        ui->label_mouseOnChannel->setText(str);
    }
}
