#include "hist2d.h"
#include "hist2ddialog.h"
#include "mvme_context.h"
#include "scrollzoomer.h"
#include "ui_hist2dwidget.h"
#include <qwt_plot_spectrogram.h>
#include <qwt_color_map.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_scale_engine.h>
#include <QDebug>
#include <QComboBox>
#include <QTimer>
#include <QtCore> // for qQNaN with Qt 5.2

//
// Hist2D
//

Hist2D::Hist2D(uint32_t xBits, uint32_t yBits, QObject *parent)
    : QObject(parent)
{
    resize(xBits, yBits);
}

Hist2D::~Hist2D()
{
    delete[] m_data;
}

void Hist2D::resize(uint32_t xBits, uint32_t yBits)
{
    m_xBits = xBits;
    m_yBits = yBits;
    delete[] m_data;
    m_data = new uint32_t[getXResolution() * getYResolution()];
    setInterval(Qt::XAxis, QwtInterval(0, getXResolution() - 1));
    setInterval(Qt::YAxis, QwtInterval(0, getYResolution() - 1));
    clear();
}

void Hist2D::clear()
{
    m_maxValue = 0;
    m_maxX = 0;
    m_maxY = 0;
    m_numberOfEntries = 0;

    for (size_t i=0; i < getXResolution() * getYResolution(); ++i)
    {
        m_data[i] = 0;
    }

    setInterval(Qt::ZAxis, QwtInterval());
}

void Hist2D::fill(uint32_t x, uint32_t y, uint32_t weight)
{
    if (x < getXResolution() && y < getYResolution())
    {
        m_data[y * getXResolution() + x] += weight;
        uint32_t value = m_data[y * getXResolution() + x];

        if (value >= m_maxValue)
        {
            m_maxValue = value;
            m_maxX = x;
            m_maxY = y;
        }
        ++m_numberOfEntries;

        setInterval(Qt::ZAxis, QwtInterval(0, m_maxValue));
    }
}

double Hist2D::value(double x, double y) const
{
    uint32_t ix = (uint32_t)x;
    uint32_t iy = (uint32_t)y;
    uint32_t v  = 0;

    if (ix < getXResolution() && iy < getYResolution())
    {
        v = m_data[iy * getXResolution() + ix];
    }

    return v > 0 ? v : qQNaN();
}

void Hist2D::setInterval(Qt::Axis axis, const QwtInterval &interval)
{
    m_intervals[axis] = interval;
}

Hist2DRasterData *Hist2D::makeRasterData()
{
    return new Hist2DRasterData(this);
}

//
// Hist2DWidget
//
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

// from http://stackoverflow.com/a/9021841
class LogarithmicColorMap : public QwtLinearColorMap
{
    public:
        LogarithmicColorMap(const QColor &from, const QColor &to)
            : QwtLinearColorMap(from, to)
        {
        }

        QRgb rgb(const QwtInterval &interval, double value) const
        {
            return QwtLinearColorMap::rgb(QwtInterval(std::log(interval.minValue()),
                                                      std::log(interval.maxValue())),
                                          std::log(value));
        }
};

Hist2DWidget::Hist2DWidget(MVMEContext *context, Hist2D *hist2d, QWidget *parent)
    : MVMEWidget(parent)
    , ui(new Ui::Hist2DWidget)
    , m_context(context)
    , m_hist2d(hist2d)
    , m_plotItem(new QwtPlotSpectrogram)
    , m_replotTimer(new QTimer(this))
{
    ui->setupUi(this);


    connect(ui->pb_export, &QPushButton::clicked, this, &Hist2DWidget::exportPlot);

    connect(ui->pb_clear, &QPushButton::clicked, this, [this] {
        m_hist2d->clear();
        replot();
    });

    connect(ui->linLogGroup, SIGNAL(buttonClicked(int)), this, SLOT(displayChanged()));

    ui->pb_edit->setVisible(false);
    // FIXME: reenable or remove the edit button!
    //connect(ui->pb_edit, &QPushButton::clicked, this, [this] {
    //    Hist2DDialog dialog(m_context, m_hist2d, this);
    //    int result = dialog.exec();
    //    if (result == QDialog::Accepted)
    //    {
    //        dialog.getHist2D(); // this updates the histogram
    //    }
    //});

    auto histData = m_hist2d->makeRasterData();
    m_plotItem->setData(histData);
    m_plotItem->setRenderThreadCount(0); // use system specific ideal thread count
    m_plotItem->setColorMap(getColorMap());
    m_plotItem->attach(ui->plot);

    auto rightAxis = ui->plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Counts");
    rightAxis->setColorBarEnabled(true);
    ui->plot->enableAxis(QwtPlot::yRight);

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

    auto config = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_hist2d, QSL("ObjectToConfig")));

    if (config)
        connect(config, &ConfigObject::modified, this, &Hist2DWidget::displayChanged);

    displayChanged();
}

Hist2DWidget::~Hist2DWidget()
{
    delete m_plotItem;
    delete ui;
}

void Hist2DWidget::replot()
{
    QString xTitle;
    QString yTitle;

    auto config = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_hist2d, QSL("ObjectToConfig")));

    if (config)
    {
        xTitle = config->property("xAxisTitle").toString();
        ui->plot->axisWidget(QwtPlot::xBottom)->setTitle(xTitle);
        yTitle = config->property("yAxisTitle").toString();
        ui->plot->axisWidget(QwtPlot::yLeft)->setTitle(yTitle);

        setWindowTitle(QString("%1 - %2 | %3")
                       .arg(config->objectName())
                       .arg(xTitle)
                       .arg(yTitle));
    }

    // z
    auto histData = reinterpret_cast<Hist2DRasterData *>(m_plotItem->data());
    histData->updateIntervals();
    auto interval = histData->interval(Qt::ZAxis);
    ui->plot->setAxisScale(QwtPlot::yRight, interval.minValue(), interval.maxValue());
    auto axis = ui->plot->axisWidget(QwtPlot::yRight);
    axis->setColorMap(interval, getColorMap());

    ui->plot->replot();

    ui->label_numberOfEntries->setText(QString::number(m_hist2d->getNumberOfEntries()));
    ui->label_maxValue->setText(QString::number(m_hist2d->getMaxValue()));
    ui->label_maxX->setText(QString::number(m_hist2d->getMaxX()));
    ui->label_maxY->setText(QString::number(m_hist2d->getMaxY()));
}

void Hist2DWidget::displayChanged()
{
    if (ui->scaleLin->isChecked() && !zAxisIsLin())
    {
        ui->plot->setAxisScaleEngine(QwtPlot::yRight, new QwtLinearScaleEngine);
        ui->plot->setAxisAutoScale(QwtPlot::yRight, true);
    }
    else if (ui->scaleLog->isChecked() && !zAxisIsLog())
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        ui->plot->setAxisScaleEngine(QwtPlot::yRight, scaleEngine);
    }

    m_plotItem->setColorMap(getColorMap());

    replot();
}

void Hist2DWidget::exportPlot()
{
    QString fileName = m_hist2d->objectName();
    QwtPlotRenderer renderer;
    renderer.exportTo(ui->plot, fileName);
}

bool Hist2DWidget::zAxisIsLog() const
{
    return dynamic_cast<QwtLogScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yRight));
}

bool Hist2DWidget::zAxisIsLin() const
{
    return dynamic_cast<QwtLinearScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yRight));
}

QwtLinearColorMap *Hist2DWidget::getColorMap() const
{
    auto colorFrom = Qt::darkBlue;
    auto colorTo   = Qt::darkRed;
    QwtLinearColorMap *colorMap = nullptr;

    if (zAxisIsLin())
    {
        colorMap = new QwtLinearColorMap(colorFrom, colorTo);
    }
    else
    {
        colorMap = new LogarithmicColorMap(colorFrom, colorTo);
    }

    colorMap->addColorStop(0.2, Qt::blue);
    colorMap->addColorStop(0.4, Qt::cyan);
    colorMap->addColorStop(0.6, Qt::yellow);
    colorMap->addColorStop(0.8, Qt::red);

    colorMap->setMode(QwtLinearColorMap::ScaledColors);

    return colorMap;
}

