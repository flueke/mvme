#include "hist2d.h"
#include "ui_hist2dwidget.h"
#include "scrollzoomer.h"
#include <qwt_plot_spectrogram.h>
#include <qwt_color_map.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <QDebug>
#include <QComboBox>
#include <QTimer>
#include <QtCore> // for qQNaN with Qt 5.2

class Hist2DData: public QwtRasterData
{
public:

    Hist2DData(uint32_t xResolution, uint32_t yResolution)
        : m_xResolution(xResolution)
        , m_yResolution(yResolution)
        , m_data(new uint32_t[xResolution * yResolution])
        , m_maxValue(0)
    {
        setInterval(Qt::XAxis, QwtInterval(0, xResolution-1));
        setInterval(Qt::YAxis, QwtInterval(0, yResolution-1));

        reset();
    }

    ~Hist2DData()
    {
        qDebug() << __PRETTY_FUNCTION__ ;
        delete[] m_data;
    }

    void reset()
    {
        m_maxValue = 0;

        for (size_t i=0; i < m_xResolution * m_yResolution; ++i)
        {
            m_data[i] = 0;
        }

        setInterval(Qt::ZAxis, QwtInterval());
    }

    virtual double value(double x, double y) const
    {
        uint32_t ix = (uint32_t)x;
        uint32_t iy = (uint32_t)y;
        uint32_t v  = 0;

        if (ix < m_xResolution && iy < m_yResolution)
        {
            v = m_data[iy * m_xResolution + ix];
        }

        return v > 0 ? v : qQNaN();
    }

#if 0
    virtual void initRaster(const QRectF &area, const QSize &raster)
    {
        qDebug() << "initRaster() area =" << area << ", raster =" << raster;
    }
#endif

    void incValue(uint32_t x, uint32_t y)
    {
        if (x < m_xResolution && y < m_yResolution)
        {
            uint32_t value = ++m_data[y * m_xResolution + x];
            m_maxValue = qMax(value, m_maxValue);
            setInterval(Qt::ZAxis, QwtInterval(0, m_maxValue));
        }
    }

    void setValue(uint32_t x, uint32_t y, uint32_t value)
    {
        if (x < m_xResolution && y < m_yResolution)
        {
            /* XXX(flueke): this produces a wrong max value if the
             * current max value is overwritten with a smaller value. */
            m_data[y * m_xResolution + x] = value;
            m_maxValue = qMax(value, m_maxValue);
            setInterval(Qt::ZAxis, QwtInterval(0, m_maxValue));
        }
    }

    uint32_t m_xResolution;
    uint32_t m_yResolution;

private:
    uint32_t *m_data;
    uint32_t m_maxValue;
};

Hist2D::Hist2D(uint32_t xResolution, uint32_t yResolution, QObject *parent)
    : QObject(parent)
    , m_data(new Hist2DData(xResolution, yResolution))
    , m_xAxisChannel(-1)
    , m_yAxisChannel(-1)
{
}

uint32_t Hist2D::xAxisResolution() const
{
    return m_data->m_xResolution;
}

uint32_t Hist2D::yAxisResolution() const
{
    return m_data->m_yResolution;
}

QwtLinearColorMap *Hist2D::getColorMap() const
{
    auto colorMap = new QwtLinearColorMap(Qt::darkBlue, Qt::darkRed);
    colorMap->addColorStop(0.2, Qt::blue);
    colorMap->addColorStop(0.4, Qt::cyan);
    colorMap->addColorStop(0.6, Qt::yellow);
    colorMap->addColorStop(0.8, Qt::red);

    colorMap->setMode(QwtLinearColorMap::ScaledColors);

    return colorMap;
}

void Hist2D::fill(uint32_t x, uint32_t y)
{
    m_data->incValue(x, y);
}

void Hist2D::setXAxisChannel(int32_t channel)
{
    m_xAxisChannel = channel;
    m_data->reset();
}

void Hist2D::setYAxisChannel(int32_t channel)
{
    m_yAxisChannel = channel;
    m_data->reset();
}

void Hist2D::clear()
{
    m_data->reset();
}

Hist2DWidget::Hist2DWidget(Hist2D *hist2d, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Hist2DWidget)
    , m_hist2d(hist2d)
    , m_plotItem(new QwtPlotSpectrogram)
    , m_replotTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->comboXAxisModule->addItem(QString::number(0), 0);
    ui->comboYAxisModule->addItem(QString::number(0), 0);

    for (int channelIndex=0; channelIndex<32; ++channelIndex) {
        ui->comboXAxisChannel->addItem(QString::number(channelIndex), channelIndex);
        ui->comboYAxisChannel->addItem(QString::number(channelIndex), channelIndex);
    }

    ui->comboXAxisChannel->setCurrentIndex(m_hist2d->getXAxisChannel());
    ui->comboYAxisChannel->setCurrentIndex(m_hist2d->getYAxisChannel());

    connect(ui->comboXAxisChannel, SIGNAL(currentIndexChanged(int)), this, SLOT(setXAxisChannel(int)));
    connect(ui->comboYAxisChannel, SIGNAL(currentIndexChanged(int)), this, SLOT(setYAxisChannel(int)));

    connect(ui->pb_replot, SIGNAL(clicked()), ui->plot, SLOT(replot()));
    connect(ui->pb_addTestData, SIGNAL(clicked()), SLOT(addTestData()));
    connect(ui->pb_clear, &QPushButton::clicked, [=]() {
            m_hist2d->clear();
            });


    m_plotItem->setData(m_hist2d->getData());
    m_plotItem->setRenderThreadCount(0); // use system specific ideal thread count
    m_plotItem->setColorMap(m_hist2d->getColorMap());
    m_plotItem->attach(ui->plot);

    auto rightAxis = ui->plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Counts");
    rightAxis->setColorBarEnabled(true);
    //rightAxis->setColorMap(QwtInterval(0, 1), m_hist2d->getColorMap());
    //ui->plot->setAxisScale(QwtPlot::yRight, 0, 1);
    ui->plot->enableAxis(QwtPlot::yRight);


    auto histData = m_hist2d->getData();
    auto interval = histData->interval(Qt::XAxis);
    ui->plot->setAxisScale(QwtPlot::xBottom, interval.minValue(), interval.maxValue());

    interval = histData->interval(Qt::YAxis);
    ui->plot->setAxisScale(QwtPlot::yLeft, interval.minValue(), interval.maxValue());

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(2000);

    m_zoomer = new ScrollZoomer(ui->plot->canvas());
    m_zoomer->setZoomBase();

    auto plotPanner = new QwtPlotPanner(ui->plot->canvas());
    plotPanner->setMouseButton(Qt::MiddleButton);

    auto plotMagnifier = new QwtPlotMagnifier(ui->plot->canvas());
    plotMagnifier->setMouseButton(Qt::NoButton);

    replot();
}

Hist2DWidget::~Hist2DWidget()
{
    qDebug() << __PRETTY_FUNCTION__;
    m_plotItem->setData(nullptr);
    m_plotItem->detach();
    delete m_plotItem;
    delete ui;
}

void Hist2DWidget::replot()
{
    auto histData = m_hist2d->getData();

    // x
    auto axis = ui->plot->axisWidget(QwtPlot::xBottom);
    axis->setTitle(QString("Channel %1").arg(m_hist2d->getXAxisChannel()));

    // y
    axis = ui->plot->axisWidget(QwtPlot::yLeft);
    axis->setTitle(QString("Channel %1").arg(m_hist2d->getYAxisChannel()));

    // z
    auto interval = histData->interval(Qt::ZAxis);
    axis = ui->plot->axisWidget(QwtPlot::yRight);

    ui->plot->setAxisScale(QwtPlot::yRight, interval.minValue(), interval.maxValue());
    axis->setColorMap(interval, m_hist2d->getColorMap());

    ui->plot->replot();
}

void Hist2DWidget::exportPlot()
{
    QString fileName;
    fileName.sprintf("channel%02u-channel%02u.pdf",
            m_hist2d->getXAxisChannel(),
            m_hist2d->getYAxisChannel());

    QwtPlotRenderer renderer;
    renderer.exportTo(ui->plot, fileName);
}

void Hist2DWidget::addTestData()
{
    auto histData = m_hist2d->getData();

    qDebug() << "begin addRandomValues" << histData->interval(Qt::ZAxis);

    for (uint32_t x=0; x<histData->m_xResolution; ++x)
    {
        for (uint32_t y=0; y<histData->m_yResolution; ++y)
        {
            uint32_t value = x;
            histData->setValue(x, y, value);
        }
    }

    qDebug() << "end addRandomValues" << histData->interval(Qt::ZAxis);
}

void Hist2DWidget::setXAxisChannel(int channel)
{
    qDebug() << __PRETTY_FUNCTION__ << channel;
    m_hist2d->setXAxisChannel(channel);
}

void Hist2DWidget::setYAxisChannel(int channel)
{
    qDebug() << __PRETTY_FUNCTION__ << channel;
    m_hist2d->setYAxisChannel(channel);
}
