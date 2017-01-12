#include "hist2d.h"
#include "hist2ddialog.h"
#include "mvme_context.h"
#include "scrollzoomer.h"
#include "ui_hist2dwidget.h"
#include "histo_util.h"

#include <qwt_plot_spectrogram.h>
#include <qwt_color_map.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_raster_data.h>
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
    qDebug() << __PRETTY_FUNCTION__ << this << getXResolution() << getYResolution();
}

Hist2D::~Hist2D()
{
    delete[] m_data;
}

void Hist2D::resize(uint32_t xBits, uint32_t yBits)
{
    if (xBits != m_xBits || yBits != m_yBits)
    {
        qDebug() << __PRETTY_FUNCTION__ << xBits << yBits;
        m_xBits = xBits;
        m_yBits = yBits;
        auto old_data = m_data;
        m_data = new uint32_t[getXResolution() * getYResolution()];
        delete[] old_data;
        setInterval(Qt::XAxis, QwtInterval(0, getXResolution() - 1));
        setInterval(Qt::YAxis, QwtInterval(0, getYResolution() - 1));
        clear();
        emit resized(xBits, yBits);
    }
}

void Hist2D::clear()
{
    m_stats.maxValue = 0;
    m_stats.maxX = 0;
    m_stats.maxY = 0;
    m_stats.entryCount = 0;

    for (size_t i=0; i < getXResolution() * getYResolution(); ++i)
    {
        m_data[i] = 0;
    }

    setInterval(Qt::ZAxis, QwtInterval());
    m_overflow = 0.0;
}

void Hist2D::fill(uint32_t x, uint32_t y, uint32_t weight)
{
    if (x < getXResolution() && y < getYResolution())
    {
        m_data[y * getXResolution() + x] += weight;
        uint32_t value = m_data[y * getXResolution() + x];

        if (value >= m_stats.maxValue)
        {
            m_stats.maxValue = value;
            m_stats.maxX = x;
            m_stats.maxY = y;
        }
        m_stats.entryCount += weight;

        setInterval(Qt::ZAxis, QwtInterval(0, m_stats.maxValue));
    }
    else
    {
        m_overflow += 1.0;
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

Hist2DStatistics Hist2D::calcStatistics(QwtInterval xInterval, QwtInterval yInterval) const
{
    xInterval = xInterval.normalized();
    yInterval = yInterval.normalized();

    if (xInterval == interval(Qt::XAxis)
        && yInterval == interval(Qt::YAxis))
    {
        // global range for both intervals, return global stats
        return m_stats;
    }

    Hist2DStatistics result;

    u32 xMin = static_cast<u32>(std::max(xInterval.minValue(), 0.0));
    u32 xMax = static_cast<u32>(std::max(xInterval.maxValue(), 0.0));
    u32 yMin = static_cast<u32>(std::max(yInterval.minValue(), 0.0));
    u32 yMax = static_cast<u32>(std::max(yInterval.maxValue(), 0.0));

    for (u32 iy = yMin; iy < yMax; ++iy)
    {
        for (u32 ix = xMin; ix < xMax; ++ix)
        {
            // TODO: access value directly to speed this up a bit
            double v = value(ix, iy);
            if (!qIsNaN(v))
            {
                if (v > result.maxValue)
                {
                    result.maxValue = v;
                    result.maxX = ix;
                    result.maxY = iy;
                }
                result.entryCount += v;
            }
        }
    }

    return result;
}

//
// Hist2DWidget
//
class Hist2DRasterData: public QwtRasterData
{
public:

    Hist2DRasterData(Hist2D *hist2d)
        : m_hist2d(hist2d)
    {
        updateIntervals();
    }

    virtual double value(double x, double y) const
    {
        return m_hist2d->value(x, y);
    }

    void updateIntervals()
    {
        for (int axis=0; axis<3; ++axis)
        {
            setInterval(static_cast<Qt::Axis>(axis), m_hist2d->interval(static_cast<Qt::Axis>(axis)));
        }
    }

private:
    Hist2D *m_hist2d;
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
            /* XXX: Hack for log scale. Is this the right place? Limit the
             * interval somewhere else so that it is bounded to (1, X) when
             * this function is called? */
            double minValue = interval.minValue();
            if (interval.minValue() <= 0)
            {
                minValue = 1.0;
            }
            return QwtLinearColorMap::rgb(QwtInterval(std::log(minValue),
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

    ui->label_cursorInfo->setVisible(false);

    connect(hist2d, &Hist2D::resized, this, &Hist2DWidget::onHistoResized);

    connect(ui->pb_export, &QPushButton::clicked, this, &Hist2DWidget::exportPlot);

    connect(ui->pb_clear, &QPushButton::clicked, this, [this] {
        m_hist2d->clear();
        replot();
    });

    connect(ui->linLogGroup, SIGNAL(buttonClicked(int)), this, SLOT(displayChanged()));

    auto histData = new Hist2DRasterData(m_hist2d);
    m_plotItem->setData(histData);
    m_plotItem->setRenderThreadCount(0); // use system specific ideal thread count
    m_plotItem->setColorMap(getColorMap());
    m_plotItem->attach(ui->plot);

    auto rightAxis = ui->plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Counts");
    rightAxis->setColorBarEnabled(true);
    ui->plot->enableAxis(QwtPlot::yRight);

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(2000);

    ui->plot->canvas()->setMouseTracking(true);

    m_zoomer = new ScrollZoomer(ui->plot->canvas());
    m_zoomer->setZoomBase();
    connect(m_zoomer, &ScrollZoomer::zoomed, this, &Hist2DWidget::zoomerZoomed);
    connect(m_zoomer, &ScrollZoomer::mouseCursorMovedTo, this, &Hist2DWidget::mouseCursorMovedToPlotCoord);
    connect(m_zoomer, &ScrollZoomer::mouseCursorLeftPlot, this, &Hist2DWidget::mouseCursorLeftPlot);

#if 0
    auto plotPanner = new QwtPlotPanner(ui->plot->canvas());
    plotPanner->setMouseButton(Qt::MiddleButton);

    auto plotMagnifier = new QwtPlotMagnifier(ui->plot->canvas());
    plotMagnifier->setMouseButton(Qt::NoButton);
#endif

    // init to 1:1 transform
    m_xConversion.setScaleInterval(0, m_hist2d->getXResolution());
    m_xConversion.setPaintInterval(0, m_hist2d->getXResolution());
    m_yConversion.setScaleInterval(0, m_hist2d->getYResolution());
    m_yConversion.setPaintInterval(0, m_hist2d->getYResolution());

    auto config = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_hist2d, QSL("ObjectToConfig")));

    if (config)
    {
        connect(config, &ConfigObject::modified, this, &Hist2DWidget::displayChanged);
    }

    onHistoResized();
    displayChanged();
}

Hist2DWidget::~Hist2DWidget()
{
    delete m_plotItem;
    delete ui;
}

void Hist2DWidget::replot()
{
    // z axis interval
    auto histData = reinterpret_cast<Hist2DRasterData *>(m_plotItem->data());
    histData->updateIntervals();

    auto interval = histData->interval(Qt::ZAxis);
    double base = zAxisIsLog() ? 1.0 : 0.0;
    interval = interval.limited(base, interval.maxValue());

    ui->plot->setAxisScale(QwtPlot::yRight, interval.minValue(), interval.maxValue());
    auto axis = ui->plot->axisWidget(QwtPlot::yRight);
    axis->setColorMap(interval, getColorMap());

    auto stats = m_hist2d->calcStatistics(
        ui->plot->axisScaleDiv(QwtPlot::xBottom).interval(),
        ui->plot->axisScaleDiv(QwtPlot::yLeft).interval());

    double maxX = m_xConversion.transform(stats.maxX);
    double maxY = m_yConversion.transform(stats.maxY);

    ui->label_numberOfEntries->setText(QString("%L1").arg(stats.entryCount));
    ui->label_maxValue->setText(QString("%L1").arg(stats.maxValue));

    ui->label_maxX->setText(QString("%1").arg(maxX, 0, 'g', 6));
    ui->label_maxY->setText(QString("%1").arg(maxY, 0, 'g', 6));

    updateCursorInfoLabel();
    ui->plot->replot();
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
        ui->plot->setAxisAutoScale(QwtPlot::yRight, true);
    }

    m_plotItem->setColorMap(getColorMap());

    QString xTitle;
    QString yTitle;

    auto config = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_hist2d, QSL("ObjectToConfig")));

    if (config)
    {
        // x
        auto xTitle = config->getAxisTitle(Qt::XAxis);
        auto address = config->getFilterAddress(Qt::XAxis);
        xTitle.replace(QSL("%A"), QString::number(address));
        xTitle.replace(QSL("%a"), QString::number(address));
        auto unit = config->getAxisUnitLabel(Qt::XAxis);

        QString axisTitle = makeAxisTitle(xTitle, unit);
        ui->plot->axisWidget(QwtPlot::xBottom)->setTitle(axisTitle);

        // y
        QString yTitle = config->getAxisTitle(Qt::YAxis);
        address = config->getFilterAddress(Qt::YAxis);
        yTitle.replace(QSL("%A"), QString::number(address));
        yTitle.replace(QSL("%a"), QString::number(address));
        unit = config->getAxisUnitLabel(Qt::YAxis);

        axisTitle = makeAxisTitle(yTitle, unit);
        ui->plot->axisWidget(QwtPlot::yLeft)->setTitle(axisTitle);


        setWindowTitle(QString("%1 - %2 | %3")
                       .arg(config->objectName())
                       .arg(xTitle)
                       .arg(yTitle)
                      );
    }

    {
        double unitMin = config->getUnitMin(Qt::XAxis);
        double unitMax = config->getUnitMax(Qt::XAxis);
        if (std::abs(unitMax - unitMin) > 0.0)
            m_xConversion.setPaintInterval(unitMin, unitMax);
        else
            m_xConversion.setPaintInterval(0, m_hist2d->getXResolution());

        auto scaleDraw = new UnitConversionAxisScaleDraw(m_xConversion);
        ui->plot->setAxisScaleDraw(QwtPlot::xBottom, scaleDraw);

        auto scaleEngine = new UnitConversionLinearScaleEngine(m_xConversion);
        ui->plot->setAxisScaleEngine(QwtPlot::xBottom, scaleEngine);
    }

    {
        double unitMin = config->getUnitMin(Qt::YAxis);
        double unitMax = config->getUnitMax(Qt::YAxis);
        if (std::abs(unitMax - unitMin) > 0.0)
            m_yConversion.setPaintInterval(unitMin, unitMax);
        else
            m_yConversion.setPaintInterval(0, m_hist2d->getYResolution());

        auto scaleDraw = new UnitConversionAxisScaleDraw(m_yConversion);
        ui->plot->setAxisScaleDraw(QwtPlot::yLeft, scaleDraw);

        auto scaleEngine = new UnitConversionLinearScaleEngine(m_yConversion);
        ui->plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
    }

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

void Hist2DWidget::onHistoResized()
{
    m_xConversion.setScaleInterval(0, m_hist2d->getXResolution());
    m_yConversion.setScaleInterval(0, m_hist2d->getYResolution());

    auto histData = reinterpret_cast<Hist2DRasterData *>(m_plotItem->data());
    histData->updateIntervals();

    auto interval = histData->interval(Qt::XAxis);
    ui->plot->setAxisScale(QwtPlot::xBottom, interval.minValue(), interval.maxValue());

    interval = histData->interval(Qt::YAxis);
    ui->plot->setAxisScale(QwtPlot::yLeft, interval.minValue(), interval.maxValue());

    m_zoomer->setZoomBase(true);
    displayChanged();
}

void Hist2DWidget::mouseCursorMovedToPlotCoord(QPointF pos)
{
    ui->label_cursorInfo->setVisible(true);
    m_cursorPosition = pos;
    updateCursorInfoLabel();
}

void Hist2DWidget::mouseCursorLeftPlot()
{
    ui->label_cursorInfo->setVisible(false);
}

void Hist2DWidget::zoomerZoomed(const QRectF &zoomRect)
{
    // do not zoom into negatives or above the upper bin

    // x
    auto scaleDiv = ui->plot->axisScaleDiv(QwtPlot::xBottom);
    auto maxValue = m_hist2d->interval(Qt::XAxis).maxValue();

    if (scaleDiv.lowerBound() < 0.0)
        scaleDiv.setLowerBound(0.0);

    if (scaleDiv.upperBound() > maxValue)
        scaleDiv.setUpperBound(maxValue);

    ui->plot->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);

    // y
    scaleDiv = ui->plot->axisScaleDiv(QwtPlot::yLeft);
    maxValue = m_hist2d->interval(Qt::YAxis).maxValue();

    if (scaleDiv.lowerBound() < 0.0)
        scaleDiv.setLowerBound(0.0);

    if (scaleDiv.upperBound() > maxValue)
        scaleDiv.setUpperBound(maxValue);

    ui->plot->setAxisScaleDiv(QwtPlot::yLeft, scaleDiv);
    replot();
}

void Hist2DWidget::updateCursorInfoLabel()
{
    if (ui->label_cursorInfo->isVisible())
    {
        double ix = std::max(m_cursorPosition.x(), 0.0);
        double iy = std::max(m_cursorPosition.y(), 0.0);
        double value = m_hist2d->value(ix, iy);

        if (qIsNaN(value))
            value = 0.0;

        double x = m_xConversion.transform(ix);
        double y = m_yConversion.transform(iy);

        auto text =
            QString("x=%1\n"
                    "y=%2\n"
                    "z=%3\n"
                    "xbin=%4\n"
                    "ybin=%5"
                   )
            .arg(x, 0, 'g', 6)
            .arg(y, 0, 'g', 6)
            .arg(value)
            .arg(ix)
            .arg(iy)
            ;

        ui->label_cursorInfo->setText(text);
    }
}

void Hist2DWidget::on_pb_subHisto_clicked()
{
    auto xBinRange = ui->plot->axisScaleDiv(QwtPlot::xBottom).interval();
    xBinRange.setMaxValue(xBinRange.maxValue() + 1.0);

    auto yBinRange = ui->plot->axisScaleDiv(QwtPlot::yLeft).interval();
    yBinRange.setMaxValue(yBinRange.maxValue() + 1.0);


    Hist2DDialog dialog(m_context, m_hist2d, xBinRange, yBinRange, this);

    if (dialog.exec() == QDialog::Accepted)
    {
        auto result = dialog.getHistoAndConfig();

        auto histo = result.first;
        auto histoConfig = result.second;
        histo->setProperty("configId", histoConfig->getId()); // TODO: remove this. needs an update in mvme_event_processor.cc!
        m_context->registerObjectAndConfig(histo, histoConfig);
        m_context->getAnalysisConfig()->addHist2DConfig(histoConfig);
        m_context->openInNewWindow(histo);
    }
}

void Hist2DWidget::on_tb_info_clicked()
{
    auto histoConfig = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_hist2d, QSL("ObjectToConfig")));

    if (!histoConfig) return;

    auto dialogPtr = new QDialog(this);
    auto &dialog = *dialogPtr;
    dialog.setWindowTitle("Histogram Information");


    QFormLayout layout(&dialog);

    auto label = new QLabel(m_hist2d->objectName());
    label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    layout.addRow("Name", label);

    auto addAxisInfo = [this, &layout, histoConfig](Qt::Axis axis)
    {

        auto filterConfig = m_context->getAnalysisConfig()->findChildById<DataFilterConfig *>(histoConfig->getFilterId(axis));

        int prevIndex = std::max(layout.rowCount() , 0);

        layout.addRow("Bits / Resolution", new QLabel(QString("%1 / %2")
                                                      .arg(histoConfig->getBits(axis))
                                                      .arg(1 << histoConfig->getBits(axis))));

        layout.addRow("Filter / Address", new QLabel(QString("%1 / %2")
                                                   .arg(filterConfig->objectName())
                                                   .arg(histoConfig->getFilterAddress(axis))));

        layout.addRow("Filter Data Bits", new QLabel(QString::number(filterConfig->getDataBits())));

        layout.addRow("Data Shift",  new QLabel(QString::number(histoConfig->getShift(axis))));
        layout.addRow("Bin Offset", new QLabel(QString::number(histoConfig->getOffset(axis))));
        layout.addRow("Unit min/max", new QLabel(QString("[%1, %2] %3")
                                                 .arg(histoConfig->getUnitMin(axis))
                                                 .arg(histoConfig->getUnitMax(axis))
                                                 .arg(histoConfig->getAxisUnitLabel(axis))));

        for (int i = prevIndex; i < layout.rowCount(); ++i)
        {
            auto item = layout.itemAt(i, QFormLayout::FieldRole);
            if (item)
            {
                auto label = qobject_cast<QLabel *>(item->widget());
                if (label)
                {
                    label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
                }
            }
        }
    };

    label = new QLabel("===== X-Axis =====");
    label->setAlignment(Qt::AlignHCenter);
    layout.addRow(label);
    addAxisInfo(Qt::XAxis);

    label = new QLabel("===== Y-Axis =====");
    label->setAlignment(Qt::AlignHCenter);
    layout.addRow(label);
    addAxisInfo(Qt::YAxis);


    auto closeButton = new QPushButton("&Close");
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout.addRow(closeButton);

    dialog.setWindowIcon(QIcon(QPixmap(":/info.png")));
    dialog.setWindowModality(Qt::NonModal);
    dialog.setAttribute(Qt::WA_DeleteOnClose);
    dialog.show();
}
