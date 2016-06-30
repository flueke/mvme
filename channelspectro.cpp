#include "channelspectro.h"
#include "ui_channelspectrowidget.h"
#include "scrollzoomer.h"
#include <qwt_plot_spectrogram.h>
#include <qwt_color_map.h>
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
    }

    virtual double value(double x, double y) const
    {
        uint32_t ix = (uint32_t)x;
        uint32_t iy = (uint32_t)y;
        uint32_t v  = 0;

        if (ix < m_xResolution && iy < m_yResolution)
        {
            v = m_data[iy * m_yResolution + ix];
        }

        return v > 0 ? v : qQNaN();
    }

    void incValue(uint32_t x, uint32_t y)
    {
        if (x < m_xResolution && y < m_yResolution)
        {
            uint32_t value = ++m_data[y * m_yResolution + x];
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

    auto colorMap = new QwtLinearColorMap(Qt::blue, Qt::red);
    colorMap->setMode(QwtLinearColorMap::ScaledColors);
    m_plotItem->setColorMap(colorMap);
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

    ui->plot->setAxisScale(QwtPlot::xBottom, 0, m_channelSpectro->getSpectroData()->interval(Qt::XAxis).maxValue());
    ui->plot->setAxisScale(QwtPlot::yLeft, 0, m_channelSpectro->getSpectroData()->interval(Qt::YAxis).maxValue());

    m_zoomer = new ScrollZoomer(ui->plot->canvas());
    m_zoomer->setZoomBase();

    m_channelSpectro->getPlotItem()->attach(ui->plot);

    connect(m_replotTimer, SIGNAL(timeout()), ui->plot, SLOT(replot()));
    m_replotTimer->start(2000);
}

ChannelSpectroWidget::~ChannelSpectroWidget()
{
    delete ui;
}

void ChannelSpectroWidget::addRandomValues()
{
    auto spectroData = m_channelSpectro->getSpectroData();
    qDebug() << spectroData->interval(Qt::ZAxis);

    for (int Repeats = 0; Repeats < 10000; ++Repeats)
    {
        uint32_t x = random_between(0, spectroData->m_xResolution-1);
        uint32_t y = random_between(0, spectroData->m_yResolution-1);
        spectroData->incValue(x, y);
    }
    qDebug() << spectroData->interval(Qt::ZAxis);
    ui->plot->replot();
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
