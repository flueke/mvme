#include "channelspectro.h"
#include "ui_channelspectrowidget.h"
#include "scrollzoomer.h"
#include <qwt_plot_spectrogram.h>
#include <qwt_color_map.h>
#include <qwt_scale_widget.h>
#include <QDebug>
#include <QComboBox>
#include <QTimer>

#if 0
    for (int Index = 0; Index < 1000000; ++Index)
    {
        uint32_t x = random_between(0, xResolution-1);
        uint32_t y = random_between(0, yResolution-1);
        m_data->incValue(x, y);
    }
#endif

#if 0
    for (uint32_t i=0; i<g_xResolution; ++i)
    {
        for (int j=0; j<10000; ++j)
        {
            m_data->incValue(i, i+1);
            m_data->incValue(i, i);
            m_data->incValue(i, i-1);

            m_data->incValue(i+1, i);
            //m_data->incValue(i, i);
            m_data->incValue(i-1, i);
        }
    }
#endif

static double lerp(double a, double t, double b)
{
    return (1-t)*a + t*b;
}

static uint32_t random_between(uint32_t min, uint32_t max)
{
    return qrand() % (max-min+1) + min;
}

class ChannelSpectroData: public QwtRasterData
{
public:

    ChannelSpectroData(uint32_t xResolution, uint32_t yResolution)
        : m_xResolution(xResolution)
        , m_yResolution(yResolution)
        , m_data(new uint32_t[xResolution * yResolution])
        , m_maxValue(0)
    {
        setInterval(Qt::XAxis, QwtInterval(0, xResolution-1));
        setInterval(Qt::YAxis, QwtInterval(0, yResolution-1));

        reset();
    }

    ~ChannelSpectroData()
    {
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
            /* FIXME(flueke): this produces a wrong max value if the
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

// TODO(flueke): allow changing resolutions _after_ construction time
ChannelSpectro::ChannelSpectro(uint32_t xResolution, uint32_t yResolution)
    : m_plotItem(new QwtPlotSpectrogram)
    , m_data(new ChannelSpectroData(xResolution, yResolution))
    , m_xAxisChannel(-1)
    , m_yAxisChannel(-1)
    , m_xValue(-1)
    , m_yValue(-1)
{
    m_plotItem->setData(m_data);
    m_plotItem->setRenderThreadCount(0); // use system specific ideal thread count

    m_plotItem->setColorMap(getColorMap());
}

QwtLinearColorMap *ChannelSpectro::getColorMap() const
{
    auto colorMap = new QwtLinearColorMap(Qt::darkBlue, Qt::darkRed);
    colorMap->addColorStop(0.2, Qt::blue);
    colorMap->addColorStop(0.4, Qt::cyan);
    colorMap->addColorStop(0.6, Qt::yellow);
    colorMap->addColorStop(0.8, Qt::red);

    colorMap->setMode(QwtLinearColorMap::ScaledColors);

    return colorMap;
}

void ChannelSpectro::setXAxisChannel(int32_t channel)
{
    m_xAxisChannel = channel;
    m_data->reset();
    m_plotItem->itemChanged();
}

void ChannelSpectro::setYAxisChannel(int32_t channel)
{
    m_yAxisChannel = channel;
    m_data->reset();
    m_plotItem->itemChanged();
}

void ChannelSpectro::setValue(uint32_t channel, uint32_t value)
{
    if (m_xAxisChannel >= 0 && (int32_t)channel == m_xAxisChannel)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "x" <<  channel << value;
        m_xValue = value;
    }

    if (m_yAxisChannel >= 0 && (int32_t)channel == m_yAxisChannel)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "y" <<  channel << value;
        m_yValue = value;
    }

    if (m_xValue >= 0 && m_yValue >= 0)
    {
        //qDebug() << __PRETTY_FUNCTION__ << "x && y" << m_xValue << m_yValue;
        m_data->incValue(m_xValue, m_yValue);
        m_xValue = -1;
        m_yValue = -1;
        m_plotItem->itemChanged();
    }
}

void ChannelSpectro::clear()
{
    m_data->reset();
    m_plotItem->itemChanged();
}

ChannelSpectroWidget::ChannelSpectroWidget(ChannelSpectro *channelSpectro, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChannelSpectroWidget)
    , m_channelSpectro(channelSpectro)
    , m_replotTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->comboXAxisModule->addItem(QString::number(0), 0);
    ui->comboYAxisModule->addItem(QString::number(0), 0);

    for (int channelIndex=0; channelIndex<16; ++channelIndex) {
        ui->comboXAxisChannel->addItem(QString::number(channelIndex), channelIndex);
        ui->comboYAxisChannel->addItem(QString::number(channelIndex), channelIndex);
    }

    ui->comboXAxisChannel->setCurrentIndex(m_channelSpectro->getXAxisChannel());
    ui->comboYAxisChannel->setCurrentIndex(m_channelSpectro->getYAxisChannel());

    connect(ui->comboXAxisChannel, SIGNAL(currentIndexChanged(int)), this, SLOT(setXAxisChannel(int)));
    connect(ui->comboYAxisChannel, SIGNAL(currentIndexChanged(int)), this, SLOT(setYAxisChannel(int)));

    connect(ui->pb_replot, SIGNAL(clicked()), ui->plot, SLOT(replot()));
    connect(ui->pb_addRandom, SIGNAL(clicked()), SLOT(addRandomValues()));
    connect(ui->pb_clear, &QPushButton::clicked, [=]() {
            m_channelSpectro->clear();
            });


    m_channelSpectro->getPlotItem()->attach(ui->plot);

    auto rightAxis = ui->plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Counts");
    rightAxis->setColorBarEnabled(true);
    //rightAxis->setColorMap(QwtInterval(0, 1), m_channelSpectro->getColorMap());
    //ui->plot->setAxisScale(QwtPlot::yRight, 0, 1);
    ui->plot->enableAxis(QwtPlot::yRight);


    auto spectroData = m_channelSpectro->getSpectroData();
    auto interval = spectroData->interval(Qt::XAxis);
    ui->plot->setAxisScale(QwtPlot::xBottom, interval.minValue(), interval.maxValue());

    interval = spectroData->interval(Qt::YAxis);
    ui->plot->setAxisScale(QwtPlot::yLeft, interval.minValue(), interval.maxValue());

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(2000);

    m_zoomer = new ScrollZoomer(ui->plot->canvas());
    m_zoomer->setZoomBase();
    
    replot();
}

ChannelSpectroWidget::~ChannelSpectroWidget()
{
    delete ui;
}

void ChannelSpectroWidget::replot()
{
    auto spectroData = m_channelSpectro->getSpectroData();

    // x
    auto axis = ui->plot->axisWidget(QwtPlot::xBottom);
    axis->setTitle(QString("Channel %1").arg(m_channelSpectro->getXAxisChannel()));

    // y
    axis = ui->plot->axisWidget(QwtPlot::yLeft);
    axis->setTitle(QString("Channel %1").arg(m_channelSpectro->getYAxisChannel()));

    // z
    auto interval = spectroData->interval(Qt::ZAxis);
    axis = ui->plot->axisWidget(QwtPlot::yRight);

    ui->plot->setAxisScale(QwtPlot::yRight, interval.minValue(), interval.maxValue());
    axis->setColorMap(interval, m_channelSpectro->getColorMap());

    ui->plot->replot();
}

void ChannelSpectroWidget::addRandomValues()
{
    auto spectroData = m_channelSpectro->getSpectroData();

    qDebug() << "begin addRandomValues" << spectroData->interval(Qt::ZAxis);
#if 0

    for (int Repeats = 0; Repeats < 10000; ++Repeats)
    {
        uint32_t x = random_between(0, spectroData->m_xResolution-1);
        uint32_t y = random_between(0, spectroData->m_yResolution-1);
        spectroData->incValue(x, y);
    }
    qDebug() << spectroData->interval(Qt::ZAxis);
    ui->plot->replot();
#endif

#if 1
    for (uint32_t x=0; x<spectroData->m_xResolution; ++x)
    {
        for (uint32_t y=0; y<spectroData->m_yResolution; ++y)
        {
            uint32_t value = x;
            spectroData->setValue(x, y, value);
        }
    }
#endif


#if 0
    for (uint32_t x=0; x<spectroData->m_xResolution; ++x)
    {
        for (uint32_t y=0; y<spectroData->m_yResolution; ++y)
        {
            const double c = 0.842;
            const double v1 = x * x + ( y - c ) * ( y + c );
            const double v2 = x * ( y + c ) + x * ( y + c );

            double value = ( v1 * v1 + v2 * v2 );

            spectroData->setValue(x, y, value);
        }
    }
#endif

    qDebug() << "end addRandomValues" << spectroData->interval(Qt::ZAxis);
}

void ChannelSpectroWidget::setXAxisChannel(int channel)
{
    qDebug() << __PRETTY_FUNCTION__ << channel;
    m_channelSpectro->setXAxisChannel(channel);
}

void ChannelSpectroWidget::setYAxisChannel(int channel)
{
    qDebug() << __PRETTY_FUNCTION__ << channel;
    m_channelSpectro->setYAxisChannel(channel);
}
