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
#include <QDebug>
#include <QComboBox>
#include <QTimer>
#include <QtCore> // for qQNaN with Qt 5.2

//
// Hist2D
//

Hist2D::Hist2D(uint32_t xBits, uint32_t yBits, QObject *parent)
    : QObject(parent)
    , m_xBits(xBits)
    , m_yBits(yBits)
    , m_maxValue(0)
{
    m_data = new uint32_t[getXResolution() * getYResolution()];
    setInterval(Qt::XAxis, QwtInterval(0, getXResolution() - 1));
    setInterval(Qt::YAxis, QwtInterval(0, getYResolution() - 1));
    clear();
}

Hist2D::~Hist2D()
{
    delete[] m_data;
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

//
// Hist2DRasterData
//
Hist2DRasterData *Hist2D::makeRasterData()
{
    return new Hist2DRasterData(this);
}

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
    m_plotItem->setColorMap(m_hist2d->getColorMap());
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

    replot();
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
    axis->setColorMap(interval, m_hist2d->getColorMap());

    ui->plot->replot();

    ui->label_numberOfEntries->setText(QString::number(m_hist2d->getNumberOfEntries()));
    ui->label_maxValue->setText(QString::number(m_hist2d->getMaxValue()));
    ui->label_maxX->setText(QString::number(m_hist2d->getMaxX()));
    ui->label_maxY->setText(QString::number(m_hist2d->getMaxY()));
}

void Hist2DWidget::exportPlot()
{
    QString fileName = m_hist2d->objectName();
    QwtPlotRenderer renderer;
    renderer.exportTo(ui->plot, fileName);
}

void Hist2DWidget::addTestData()
{
#if 0
    qDebug() << "begin addTestData" << m_hist2d->interval(Qt::ZAxis);

    for (uint32_t x=0; x<m_hist2d->xAxisResolution(); ++x)
    {
        for (uint32_t y=0; y<m_hist2d->yAxisResolution(); ++y)
        {
            uint32_t weight = x;
            m_hist2d->fill(x, y, weight);
        }
    }

    qDebug() << "end addTestData" << m_hist2d->interval(Qt::ZAxis);
#endif
}
