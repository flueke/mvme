#include "hist1d.h"
#include "ui_hist1dwidget.h"
#include "scrollzoomer.h"
#include "mvme_config.h"
#include "mvme_context.h"
#include "histo_util.h"

#include <cmath>

#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_text.h>

#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

//
// Hist1D
//
Hist1D::Hist1D(u32 bits, QObject *parent)
    : QObject(parent)
    , m_bits(bits)
{
    m_data = new u32[getResolution()];
    clear();
    qDebug() << __PRETTY_FUNCTION__ << this << getResolution();
}

Hist1D::~Hist1D()
{
    delete[] m_data;
}

double Hist1D::value(u32 x) const
{
    u32 v = 0;

    if (x < getResolution())
    {
        v = m_data[x];
    }

    return v;
    //return v > 0 ? v : qQNaN();
}

void Hist1D::fill(u32 x, u32 weight)
{
    if (x < getResolution())
    {
        m_data[x] += weight;
        m_count += weight;
        u32 value = m_data[x];
        
        if (value >= m_maxValue)
        {
            m_maxValue = value;
            m_maxChannel = x;
        }
    }
}

void Hist1D::clear()
{
    m_count = 0;
    m_maxValue = 0;
    m_maxChannel = 0;

    for (size_t i=0; i<getResolution(); ++i)
        m_data[i] = 0;
}

Hist1DStatistics Hist1D::calcStatistics(u32 startChannel, u32 onePastEndChannel) const
{
    if (startChannel > onePastEndChannel)
        std::swap(startChannel, onePastEndChannel);

    startChannel = std::min(startChannel, getResolution());
    onePastEndChannel = std::min(onePastEndChannel, getResolution());

    Hist1DStatistics result;

    for (u32 i = startChannel; i < onePastEndChannel; ++i)
    {
        double v = value(i);
        result.mean += v * i;
        result.entryCount += v;

        if (v > result.maxValue)
        {
            result.maxValue = v;
            result.maxChannel = i;
        }
    }

    if (result.entryCount)
        result.mean /= result.entryCount;
    else
        result.mean = qQNaN();

    if (result.mean > 0 && result.entryCount > 0)
    {
        for (u32 i = startChannel; i < onePastEndChannel; ++i)
        {
            u32 v = value(i);
            if (v)
            {
                double d = i - result.mean;
                d *= d;
                result.sigma += d * v;
            }
        }
        result.sigma = sqrt(result.sigma / result.entryCount);
    }

    return result;
}

QTextStream &writeHistogram(QTextStream &out, Hist1D *histo)
{
    for (quint32 valueIndex=0; valueIndex<histo->getResolution(); ++valueIndex)
    {
        out << valueIndex << " "
            << histo->value(valueIndex)
            << endl;
    }

    return out;
}

Hist1D *readHistogram(QTextStream &in)
{
    double value;
    QVector<double> values;

    while (true)
    {
        u32 channelIndex;
        in >> channelIndex >> value;
        if (in.status() != QTextStream::Ok)
            break;
        values.push_back(value);
    }

    u32 bits = std::ceil(std::log2(values.size()));

    qDebug() << values.size() << bits;

    auto result = new Hist1D(bits);

    for (int channelIndex = 0;
         channelIndex < values.size();
        ++channelIndex)
    {
        result->fill(channelIndex, static_cast<u32>(values[channelIndex]));
    }

    return result;
}

//
// Hist1DWidget
//
class Hist1DIntervalData: public QwtSeriesData<QwtIntervalSample>
{
    public:
        Hist1DIntervalData(Hist1D *histo)
            : m_histo(histo)
        {}

        virtual size_t size() const override
        {
            return m_histo->getResolution();
        }

        virtual QwtIntervalSample sample(size_t i) const override
        {
            return QwtIntervalSample(m_histo->value(i), i, i+1);
        }

        virtual QRectF boundingRect() const override
        {
            return QRectF(0, 0, m_histo->getResolution(), m_histo->getMaxValue());
        }

    private:
        Hist1D *m_histo;
};

class Hist1DPointData: public QwtSeriesData<QPointF>
{
    public:
        Hist1DPointData(Hist1D *histo)
            : m_histo(histo)
        {}

        virtual size_t size() const override
        {
            return m_histo->getResolution();
        }

        virtual QPointF sample(size_t i) const override
        {
            return QPointF(i, m_histo->value(i));
        }

        virtual QRectF boundingRect() const override
        {
            return QRectF(0, 0, m_histo->getResolution(), m_histo->getMaxValue());
        }

    private:
        Hist1D *m_histo;
};

Hist1DWidget::Hist1DWidget(MVMEContext *context, Hist1D *histo, QWidget *parent)
    : Hist1DWidget(context, histo, nullptr, parent)
{}

Hist1DWidget::Hist1DWidget(MVMEContext *context, Hist1D *histo, Hist1DConfig *histoConfig, QWidget *parent)
    : MVMEWidget(parent)
    , ui(new Ui::Hist1DWidget)
    , m_context(context)
    , m_histo(histo)
    , m_histoConfig(histoConfig)
    //, m_plotHisto(new QwtPlotHistogram)
    , m_plotCurve(new QwtPlotCurve)
    , m_replotTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->label_cursorInfo->setVisible(false);

    connect(ui->pb_export, &QPushButton::clicked, this, &Hist1DWidget::exportPlot);
    connect(ui->pb_save, &QPushButton::clicked, this, &Hist1DWidget::saveHistogram);

    connect(ui->pb_clear, &QPushButton::clicked, this, [this] {
        m_histo->clear();
        replot();
    });

    connect(ui->linLogGroup, SIGNAL(buttonClicked(int)), this, SLOT(displayChanged()));

    //m_plotHisto->setData(new Hist1DIntervalData(m_histo));
    //m_plotHisto->attach(ui->plot);

    m_plotCurve->setData(new Hist1DPointData(m_histo));
    m_plotCurve->setStyle(QwtPlotCurve::Steps);
    m_plotCurve->setCurveAttribute(QwtPlotCurve::Inverted);
    m_plotCurve->attach(ui->plot);

    ui->plot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(2000);

    ui->plot->canvas()->setMouseTracking(true);

    m_zoomer = new ScrollZoomer(ui->plot->canvas());

    // assign the unused yRight axis to only zoom in x
    // Note: I disabled this as it caused a wrong y value to be displayed on
    // the tracker text when creating a zoom rectangle. The effect of not
    // zooming into y is achieved by setting a fixed yAxis after a zoom
    // operation.
    //m_zoomer->setAxis(QwtPlot::xBottom, QwtPlot::yRight);
    m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_zoomer->setZoomBase();

    connect(m_zoomer, &ScrollZoomer::zoomed, this, &Hist1DWidget::zoomerZoomed);
    connect(m_zoomer, &ScrollZoomer::mouseCursorMovedTo, this, &Hist1DWidget::mouseCursorMovedToPlotCoord);
    connect(m_zoomer, &ScrollZoomer::mouseCursorLeftPlot, this, &Hist1DWidget::mouseCursorLeftPlot);

#if 0
    auto plotPanner = new QwtPlotPanner(ui->plot->canvas());
    plotPanner->setAxisEnabled(QwtPlot::yLeft, false);
    plotPanner->setMouseButton(Qt::MiddleButton);
#endif

#if 0
    auto plotMagnifier = new QwtPlotMagnifier(ui->plot->canvas());
    plotMagnifier->setAxisEnabled(QwtPlot::yLeft, false);
    plotMagnifier->setMouseButton(Qt::NoButton);
#endif

    m_statsText = new QwtText;
    m_statsText->setRenderFlags(Qt::AlignLeft | Qt::AlignTop);

    m_statsTextItem = new QwtPlotTextLabel;
    m_statsTextItem->setText(*m_statsText);
    m_statsTextItem->attach(ui->plot);

    // init to 1:1 transform
    m_conversionMap.setScaleInterval(0, m_histo->getResolution());
    m_conversionMap.setPaintInterval(0, m_histo->getResolution());

    if (m_histoConfig)
    {
        connect(m_histoConfig, &ConfigObject::modified, this, &Hist1DWidget::displayChanged);

    }

    displayChanged();
}

Hist1DWidget::~Hist1DWidget()
{
    delete ui;
}

void Hist1DWidget::replot()
{
    updateStatistics();
    updateAxisScales();
    updateCursorInfoLabel();
    ui->plot->replot();
}

void Hist1DWidget::displayChanged()
{
    if (ui->scaleLin->isChecked() && !yAxisIsLin())
    {
        ui->plot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        ui->plot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (ui->scaleLog->isChecked() && !yAxisIsLog())
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        ui->plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
    }

    auto name = m_histoConfig ? m_histoConfig->objectName() : m_histo->objectName();
    setWindowTitle(QString("Histogram %1").arg(name));

    if (m_histoConfig)
    {
        auto axisTitle = makeAxisTitle(m_histoConfig->property("xAxisTitle").toString(),
                                       m_histoConfig->property("xAxisUnit").toString());

        if (!axisTitle.isEmpty())
        {
            ui->plot->axisWidget(QwtPlot::xBottom)->setTitle(axisTitle);
        }

        auto windowTitle = QSL("Histogram ") + getHistoPath(m_context, m_histoConfig);

        setWindowTitle(windowTitle);

        double unitMin = m_histoConfig->property("xAxisUnitMin").toDouble();
        double unitMax = m_histoConfig->property("xAxisUnitMax").toDouble();
        if (std::abs(unitMax - unitMin) > 0.0)
        {
            m_conversionMap.setPaintInterval(unitMin, unitMax);
        }
        else
        {
            m_conversionMap.setPaintInterval(0, m_histo->getResolution());
        }

        auto scaleDraw = new UnitConversionAxisScaleDraw(m_conversionMap);
        ui->plot->setAxisScaleDraw(QwtPlot::xBottom, scaleDraw);

        auto scaleEngine = new UnitConversionLinearScaleEngine(m_conversionMap);
        ui->plot->setAxisScaleEngine(QwtPlot::xBottom, scaleEngine);

        m_zoomer->setConversionX(m_conversionMap);
    }

    replot();
}

void Hist1DWidget::zoomerZoomed(const QRectF &zoomRect)
{
    if (m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        ui->plot->setAxisScale( QwtPlot::xBottom, 0, m_histo->getResolution());
        ui->plot->replot();
        m_zoomer->setZoomBase();
    }

    // do not zoom into negatives

    auto scaleDiv = ui->plot->axisScaleDiv(QwtPlot::xBottom);

    if (scaleDiv.lowerBound() < 0.0)
    {
        scaleDiv.setLowerBound(0.0);
        ui->plot->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);
    }

    scaleDiv = ui->plot->axisScaleDiv(QwtPlot::yLeft);

    if (scaleDiv.lowerBound() < 0.0)
    {
        scaleDiv.setLowerBound(0.0);
        ui->plot->setAxisScaleDiv(QwtPlot::yLeft, scaleDiv);
    }

    replot();
}

void Hist1DWidget::mouseCursorMovedToPlotCoord(QPointF pos)
{
    ui->label_cursorInfo->setVisible(true);
    m_cursorPosition = pos;
    updateCursorInfoLabel();
}

void Hist1DWidget::mouseCursorLeftPlot()
{
    ui->label_cursorInfo->setVisible(false);
}

void Hist1DWidget::updateStatistics()
{
    auto lowerBound = qFloor(ui->plot->axisScaleDiv(QwtPlot::xBottom).lowerBound());
    auto upperBound = qCeil(ui->plot->axisScaleDiv(QwtPlot::xBottom).upperBound());

    m_stats = m_histo->calcStatistics(lowerBound, upperBound);

    double mean = m_conversionMap.transform(m_stats.mean);
    double maxAt = m_conversionMap.transform(m_stats.maxChannel);
    // abs(unitMax - unitMin) / (histoMax - histoMin)
    double factor = std::abs((m_conversionMap.p2() - m_conversionMap.p1())) / (m_conversionMap.s2() - m_conversionMap.s1());
    double sigma = m_stats.sigma * factor;

    static const int fieldWidth = 0;
    QString buffer = QString("\nMean: %L1"
                             "\nSigma: %L2"
                             "\nCounts: %L3"
                             "\nMaximum: %L4"
                             "\nMax at: %L5\n")
        .arg(mean, fieldWidth)
        .arg(sigma, fieldWidth)
        .arg(m_stats.entryCount, fieldWidth)
        .arg(m_stats.maxValue, fieldWidth)
        .arg(maxAt, fieldWidth)
        ;

    m_statsText->setText(buffer);
    m_statsTextItem->setText(*m_statsText);
}

void Hist1DWidget::updateAxisScales()
{
    // update the y axis using the currently visible max value
    double maxValue = 1.2 * m_stats.maxValue;

    if (maxValue <= 1.0)
        maxValue = 10.0;

    // this sets a fixed y axis scale effectively overriding any changes made by the scrollzoomer
    double base = yAxisIsLog() ? 1.0 : 0.0l;
    ui->plot->setAxisScale(QwtPlot::yLeft, base, maxValue);

    // xAxis
    if (m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        ui->plot->setAxisScale(QwtPlot::xBottom, 0, m_histo->getResolution());
    }
}

bool Hist1DWidget::yAxisIsLog()
{
    return dynamic_cast<QwtLogScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yLeft));
}

bool Hist1DWidget::yAxisIsLin()
{
    return dynamic_cast<QwtLinearScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yLeft));
}

void Hist1DWidget::exportPlot()
{
    QString fileName = m_histo->objectName();
    QwtPlotRenderer renderer;
    renderer.exportTo(ui->plot, fileName);
}
void Hist1DWidget::saveHistogram()
{
    QString path = QSettings().value("Files/LastHistogramExportDirectory").toString();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    auto name = m_histo->objectName();

    QString fileName = QString("%1/%2.txt")
        .arg(path)
        .arg(name);

    qDebug() << fileName;

    fileName = QFileDialog::getSaveFileName(this, "Save Histogram", fileName, "Text Files (*.txt);; All Files (*.*)");

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".txt";
    }

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return;
    }

    QTextStream out(&outFile);
    writeHistogram(out, m_histo);

    if (out.status() == QTextStream::Ok)
    {
        fi.setFile(fileName);
        QSettings().setValue("Files/LastHistogramExportDirectory", fi.absolutePath());
    }
}

void Hist1DWidget::updateCursorInfoLabel()
{
    if (ui->label_cursorInfo->isVisible())
    {
        u32 ix = static_cast<u32>(std::max(m_cursorPosition.x(), 0.0));
        double value = m_histo->value(ix);

        QString text = QString(
                               "x=%1\n"
                               "y=%2\n"
                               "bin=%3"
                               )
            .arg(m_conversionMap.transform(ix), 0, 'g', 6)
            .arg(value)
            .arg(ix);

        ui->label_cursorInfo->setText(text);
    }
}
