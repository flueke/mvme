#include "hist1d.h"
#include "ui_hist1dwidget.h"
#include "scrollzoomer.h"

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

class Hist1DSeriesData: public QwtSeriesData<QwtIntervalSample>
{
    public:
        Hist1DSeriesData(Hist1D *histo)
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

//
// Hist1D
//
Hist1D::Hist1D(u32 bits, QObject *parent)
    : QObject(parent)
    , m_bits(bits)
{
    m_data = new u32[getResolution()];
    clear();
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
        u32 value = m_data[x];
        
        if (value >= m_maxValue)
        {
            m_maxValue = value;
            m_maxChannel = x;
        }
        ++m_count;
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
    Hist1DStatistics result;

    if (startChannel > onePastEndChannel)
        std::swap(startChannel, onePastEndChannel);

    startChannel = std::min(startChannel, getResolution());
    onePastEndChannel = std::min(onePastEndChannel, getResolution());

    for (u32 i = startChannel; i < onePastEndChannel; ++i)
    {
        u32 v = value(i);
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
        result.mean = 0.0;

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

//
// Hist1DWidget
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

Hist1DWidget::Hist1DWidget(Hist1D *histo, QWidget *parent)
    : MVMEWidget(parent)
    , ui(new Ui::Hist1DWidget)
    , m_histo(histo)
    , m_plotItem(new QwtPlotHistogram)
    , m_replotTimer(new QTimer(this))
{
    ui->setupUi(this);

    connect(ui->pb_export, &QPushButton::clicked, this, &Hist1DWidget::exportPlot);
    connect(ui->pb_save, &QPushButton::clicked, this, &Hist1DWidget::saveHistogram);

    connect(ui->pb_clear, &QPushButton::clicked, this, [this] {
        m_histo->clear();
        replot();
    });

    connect(ui->linLogGroup, SIGNAL(buttonClicked(int)), this, SLOT(displayChanged()));


    m_plotItem->setData(new Hist1DSeriesData(m_histo));
    m_plotItem->attach(ui->plot);

    ui->plot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");
    ui->plot->axisWidget(QwtPlot::xBottom)->setTitle("Channel X");

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(2000);

    m_zoomer = new ScrollZoomer(ui->plot->canvas());
    // assign the unused yRight axis to only zoom in x
    m_zoomer->setAxis(QwtPlot::xBottom, QwtPlot::yRight);
    m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_zoomer->setZoomBase();

    connect(m_zoomer, &ScrollZoomer::zoomed, this, &Hist1DWidget::zoomerZoomed);
    connect(m_zoomer, &ScrollZoomer::mouseCursorMovedTo, this, &Hist1DWidget::mouseCursorMovedToPlotCoord);

    /*
    connect(m_zoomer, SIGNAL(zoomed(QRectF)),this, SLOT(zoomerZoomed(QRectF)));
    connect(m_zoomer, SIGNAL(mouseCursorMovedTo(QPointF)), this, SLOT(mouseCursorMovedToPlotCoord(QPointF)));
    */

#if 0
    auto plotPanner = new QwtPlotPanner(ui->plot->canvas());
    plotPanner->setAxisEnabled(QwtPlot::yLeft, false);
    plotPanner->setMouseButton(Qt::MiddleButton);
#endif

    auto plotMagnifier = new QwtPlotMagnifier(ui->plot->canvas());
    plotMagnifier->setAxisEnabled(QwtPlot::yLeft, false);
    plotMagnifier->setMouseButton(Qt::NoButton);

    m_statsText = new QwtText;
    m_statsText->setRenderFlags(Qt::AlignLeft | Qt::AlignTop);

    m_statsTextItem = new QwtPlotTextLabel;
    m_statsTextItem->setText(*m_statsText);
    m_statsTextItem->attach(ui->plot);

    displayChanged();
}

Hist1DWidget::~Hist1DWidget()
{
    delete ui;
}

void Hist1DWidget::replot()
{
    updateStatistics();
    updateYAxisScale();
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

    auto name = m_histo->objectName();

    setWindowTitle(QString("Histogram %1")
                   .arg(name)
                  );

    replot();
}

void Hist1DWidget::zoomerZoomed(const QRectF &zoomRect)
{
    if (m_zoomer->zoomRectIndex() == 0)
    {
        if (yAxisIsLog())
        {
            updateYAxisScale();
        }
        else
        {
            ui->plot->setAxisAutoScale(QwtPlot::yLeft, true);
        }

        ui->plot->setAxisScale( QwtPlot::xBottom, 0, m_histo->getResolution());
        ui->plot->replot();
        m_zoomer->setZoomBase();
    }
}

void Hist1DWidget::mouseCursorMovedToPlotCoord(QPointF)
{
}

void Hist1DWidget::updateStatistics()
{
    auto lowerBound = qFloor(ui->plot->axisScaleDiv(QwtPlot::xBottom).lowerBound());
    auto upperBound = qCeil(ui->plot->axisScaleDiv(QwtPlot::xBottom).upperBound());

    auto stats = m_histo->calcStatistics(lowerBound, upperBound);

    QString buffer;
    buffer.sprintf("\nMean: %2.2f\nSigma: %2.2f\nCounts: %u\nMaximum: %u\nat Channel: %u\n",
                   stats.mean,
                   stats.sigma,
                   stats.entryCount,
                   stats.maxValue,
                   stats.maxChannel
                  );
    m_statsText->setText(buffer);
    m_statsTextItem->setText(*m_statsText);
}

void Hist1DWidget::updateYAxisScale()
{
    // update the y axis using the currently visible max value
    double maxValue = 1.2 * m_histo->getMaxValue();

    if (maxValue <= 1.0)
        maxValue = 10.0;

    double base = yAxisIsLog() ? 1.0 : 0.0l;
    ui->plot->setAxisScale(QwtPlot::yLeft, base, maxValue);
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
