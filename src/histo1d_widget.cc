#include "histo1d_widget.h"
#include "ui_histo1d_widget.h"
#include "scrollzoomer.h"
#include "util.h"
#include "analysis/analysis.h"
#include "qt-collapsible-section/Section.h"
#include "mvme_context.h"

#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_text.h>

#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

static const s32 ReplotPeriod_ms = 1000;

class Histo1DPointData: public QwtSeriesData<QPointF>
{
    public:
        Histo1DPointData(Histo1D *histo)
            : m_histo(histo)
        {}

        virtual size_t size() const override
        {
            return m_histo->getNumberOfBins();
        }

        virtual QPointF sample(size_t i) const override
        {
            auto result = QPointF(
                m_histo->getBinLowEdge(i),
                m_histo->getBinContent(i));

            return result;
        }

        virtual QRectF boundingRect() const override
        {
            // Qt and Qwt have different understanding of rectangles. For Qt
            // it's top-down like screen coordinates, for Qwt it's bottom-up
            // like the coordinates in a plot.
            //auto result = QRectF(
            //    m_histo->getXMin(),  m_histo->getMaxValue(), // top-left
            //    m_histo->getWidth(), m_histo->getMaxValue());  // width, height
            auto result = QRectF(
                m_histo->getXMin(), 0.0,
                m_histo->getWidth(), m_histo->getMaxValue());

            return result;
        }

    private:
        Histo1D *m_histo;
};

struct CalibUi
{
    QDoubleSpinBox *actual1, *actual2,
                   *target1, *target2,
                   *lastFocusedActual;
    QPushButton *applyButton,
                *fillMaxButton,
                *resetToFilterButton;
};

Histo1DWidget::Histo1DWidget(const Histo1DPtr &histoPtr, QWidget *parent)
    : Histo1DWidget(histoPtr.get(), parent)
{
    m_histoPtr = histoPtr;
}

Histo1DWidget::Histo1DWidget(Histo1D *histo, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Histo1DWidget)
    , m_histo(histo)
    , m_plotCurve(new QwtPlotCurve)
    , m_replotTimer(new QTimer(this))
    , m_cursorPosition(make_quiet_nan(), make_quiet_nan())
    , m_labelCursorInfoWidth(-1)
{
    ui->setupUi(this);

    connect(ui->pb_export, &QPushButton::clicked, this, &Histo1DWidget::exportPlot);
    connect(ui->pb_save, &QPushButton::clicked, this, &Histo1DWidget::saveHistogram);

    connect(ui->pb_clear, &QPushButton::clicked, this, [this] {
        m_histo->clear();
        replot();
    });

    connect(ui->linLogGroup, SIGNAL(buttonClicked(int)), this, SLOT(displayChanged()));

    m_plotCurve->setStyle(QwtPlotCurve::Steps);
    m_plotCurve->setCurveAttribute(QwtPlotCurve::Inverted);
    m_plotCurve->attach(ui->plot);

    ui->plot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(ReplotPeriod_ms);

    ui->plot->canvas()->setMouseTracking(true);

    m_zoomer = new ScrollZoomer(ui->plot->canvas());

    m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_zoomer->setZoomBase();
    qDebug() << "zoomRectIndex()" << m_zoomer->zoomRectIndex();

    connect(m_zoomer, &ScrollZoomer::zoomed, this, &Histo1DWidget::zoomerZoomed);
    connect(m_zoomer, &ScrollZoomer::mouseCursorMovedTo, this, &Histo1DWidget::mouseCursorMovedToPlotCoord);
    connect(m_zoomer, &ScrollZoomer::mouseCursorLeftPlot, this, &Histo1DWidget::mouseCursorLeftPlot);

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

    //
    // Stats text
    //
    m_statsText = new QwtText;
    /* This controls the alignment of the whole text on the canvas aswell as
     * the alignment of text itself. */
    m_statsText->setRenderFlags(Qt::AlignRight | Qt::AlignTop);

    QPen borderPen(Qt::SolidLine);
    borderPen.setColor(Qt::black);
    m_statsText->setBorderPen(borderPen);

    QBrush brush;
    brush.setColor("#e6e2de");
    brush.setStyle(Qt::SolidPattern);
    m_statsText->setBackgroundBrush(brush);

    /* The text rendered by qwt looked non-antialiased when using the RichText
     * format. Manually setting the pixelSize fixes this. */
    QFont font;
    font.setPixelSize(12);
    m_statsText->setFont(font);

    m_statsTextItem = new QwtPlotTextLabel;
    //m_statsTextItem->setRenderHint(QwtPlotItem::RenderAntialiased);
    /* Margin added to contentsMargins() of the canvas. This is (mis)used to
     * not clip the top scrollbar. */
    m_statsTextItem->setMargin(15);
    m_statsTextItem->setText(*m_statsText);
    //m_statsTextItem->setZ(42.0); // something > 0
    m_statsTextItem->attach(ui->plot);

    //
    // Calib Ui
    //
    m_calibUi = new CalibUi;
    m_calibUi->actual1 = new QDoubleSpinBox;
    m_calibUi->actual2 = new QDoubleSpinBox;
    m_calibUi->target1 = new QDoubleSpinBox;
    m_calibUi->target2 = new QDoubleSpinBox;
    m_calibUi->applyButton = new QPushButton(QSL("Apply"));
    m_calibUi->fillMaxButton = new QPushButton(QSL("Vis. Max"));
    m_calibUi->fillMaxButton->setToolTip(QSL("Fill the last focused actual value with the visible maximum histogram value"));
    m_calibUi->resetToFilterButton = new QPushButton(QSL("Restore"));
    m_calibUi->resetToFilterButton->setToolTip(QSL("Restore base unit values from source calibration"));

    m_calibUi->lastFocusedActual = m_calibUi->actual2;
    m_calibUi->actual1->installEventFilter(this);
    m_calibUi->actual2->installEventFilter(this);

    connect(m_calibUi->applyButton, &QPushButton::clicked, this, &Histo1DWidget::calibApply);
    connect(m_calibUi->fillMaxButton, &QPushButton::clicked, this, &Histo1DWidget::calibFillMax);
    connect(m_calibUi->resetToFilterButton, &QPushButton::clicked, this, &Histo1DWidget::calibResetToFilter);

    QVector<QDoubleSpinBox *> spins = { m_calibUi->actual1, m_calibUi->actual2, m_calibUi->target1, m_calibUi->target2 };

    for (auto spin: spins)
    {
        spin->setDecimals(4);
        spin->setSingleStep(0.0001);
        spin->setMinimum(std::numeric_limits<double>::lowest());
        spin->setMaximum(std::numeric_limits<double>::max());
        spin->setValue(0.0);
    }

    auto calibLayout = new QGridLayout;
    calibLayout->setContentsMargins(3, 3, 3, 3);
    calibLayout->setSpacing(2);

    calibLayout->addWidget(new QLabel(QSL("Actual")), 0, 0, Qt::AlignHCenter);
    calibLayout->addWidget(new QLabel(QSL("Target")), 0, 1, Qt::AlignHCenter);

    calibLayout->addWidget(m_calibUi->actual1, 1, 0);
    calibLayout->addWidget(m_calibUi->target1, 1, 1);

    calibLayout->addWidget(m_calibUi->actual2, 2, 0);
    calibLayout->addWidget(m_calibUi->target2, 2, 1);

    calibLayout->addWidget(m_calibUi->fillMaxButton, 3, 0, 1, 1);
    calibLayout->addWidget(m_calibUi->applyButton, 3, 1, 1, 1);

    calibLayout->addWidget(m_calibUi->resetToFilterButton, 4, 0, 1, 1);

    auto calibSection = new Section(QSL("Calibration"));
    calibSection->setContentLayout(*calibLayout);

    auto calibFrameLayout = new QHBoxLayout(ui->frame_calib);
    calibFrameLayout->setContentsMargins(0, 0, 0, 0);
    calibFrameLayout->addWidget(calibSection);

    // Hide the calibration UI. It will be shown if setCalibrationInfo() is called.
    ui->frame_calib->setVisible(false);

    setHistogram(histo);
}

Histo1DWidget::~Histo1DWidget()
{
    delete m_plotCurve;
    delete ui;
    delete m_statsText;
}

void Histo1DWidget::setHistogram(Histo1D *histo)
{
    m_histo = histo;
    m_plotCurve->setData(new Histo1DPointData(m_histo));

    displayChanged();
}

void Histo1DWidget::replot()
{
    updateAxisScales();
    updateStatistics();
    updateCursorInfoLabel();
    ui->plot->replot();
}

void Histo1DWidget::displayChanged()
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
    setWindowTitle(QString("Histogram %1").arg(name));

    auto axisInfo = m_histo->getAxisInfo(Qt::XAxis);
    ui->plot->axisWidget(QwtPlot::xBottom)->setTitle(make_title_string(axisInfo));

    /* Before the scale change the zoomer might have been zoomed into negative
     * x-axis bins. This results in scaling errors and a zoom into negative
     * coordinates which we don't want to allow.
     *
     * To fix this call updateAxes() on the plot to rebuild the axes, then
     * simulate a zoom event with the current zoomRect by calling
     * zoomerZoomed(). This method will then again limit the x-axis' lower
     * bound to 0.0.
     */
    ui->plot->updateAxes();
    zoomerZoomed(m_zoomer->zoomRect());

    replot();
}

void Histo1DWidget::zoomerZoomed(const QRectF &zoomRect)
{
    if (m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        ui->plot->setAxisScale(QwtPlot::xBottom, m_histo->getXMin(), m_histo->getXMax());
        ui->plot->replot();
        m_zoomer->setZoomBase();
    }

// FIXME: reenable this
#if 0
    // do not zoom outside the histogram range
    auto scaleDiv = ui->plot->axisScaleDiv(QwtPlot::xBottom);

    if (scaleDiv.lowerBound() < m_histo->getXMin())
    {
        scaleDiv.setLowerBound(m_histo->getXMin());
    }

    if (scaleDiv.upperBound() > m_histo->getXMax())
    {
        scaleDiv.setUpperBound(m_histo->getXMax());
    }

    ui->plot->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);
#endif


// FIXME: needed at all?
#if 0
    scaleDiv = ui->plot->axisScaleDiv(QwtPlot::yLeft);

    if (scaleDiv.lowerBound() < 0.0)
    {
        scaleDiv.setLowerBound(0.0);
        ui->plot->setAxisScaleDiv(QwtPlot::yLeft, scaleDiv);
    }
#endif

    replot();
}

void Histo1DWidget::mouseCursorMovedToPlotCoord(QPointF pos)
{
    m_cursorPosition = pos;
    updateCursorInfoLabel();
}

void Histo1DWidget::mouseCursorLeftPlot()
{
    m_cursorPosition = QPointF(make_quiet_nan(), make_quiet_nan());
    updateCursorInfoLabel();
}

void Histo1DWidget::updateStatistics()
{
    auto lowerBound = qFloor(ui->plot->axisScaleDiv(QwtPlot::xBottom).lowerBound());
    auto upperBound = qCeil(ui->plot->axisScaleDiv(QwtPlot::xBottom).upperBound());

    //qDebug() << __PRETTY_FUNCTION__ << lowerBound << upperBound;

    m_stats = m_histo->calcStatistics(lowerBound, upperBound);

    static const QString textTemplate = QSL(
        "<table>"
        "<tr><td align=\"left\">Sigma  </td><td>%L1</td></tr>"
        "<tr><td align=\"left\">FWHM   </td><td>%L2</td></tr>"
        "<tr><td align=\"left\">Mean   </td><td>%L3</td></tr>"
        "<tr><td align=\"left\">Max    </td><td>%L4</td></tr>"
        "<tr><td align=\"left\">Max Y  </td><td>%L5</td></tr>"
        "<tr><td align=\"left\">Counts </td><td>%L6</td></tr>"
        "</table>"
        );

    static const int fieldWidth = 0;
    QString buffer = textTemplate
        .arg(m_stats.sigma, fieldWidth)
        .arg(m_stats.fwhm)
        .arg(m_stats.mean, fieldWidth)
        .arg(m_histo->getBinLowEdge(m_stats.maxBin), fieldWidth)
        .arg(m_stats.maxValue, fieldWidth)
        .arg(m_stats.entryCount, fieldWidth)
        ;

    m_statsText->setText(buffer, QwtText::RichText);
    m_statsTextItem->setText(*m_statsText);
}

void Histo1DWidget::updateAxisScales()
{
    // update the y axis using the currently visible max value
    // 20% larger than the current maximum value
    double maxValue = 1.2 * m_stats.maxValue;

    // force a minimum of 10 units in y
    if (maxValue <= 1.0)
        maxValue = 10.0;

    // This sets a fixed y axis scale effectively overriding any changes made
    // by the scrollzoomer. Makes the y-axis start at 1.0 for logarithmic scales.
    double base = yAxisIsLog() ? 1.0 : 0.0l;
    ui->plot->setAxisScale(QwtPlot::yLeft, base, maxValue);

    // xAxis
    if (m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        ui->plot->setAxisScale(QwtPlot::xBottom, m_histo->getXMin(), m_histo->getXMax());
    }

    ui->plot->updateAxes();
}

bool Histo1DWidget::yAxisIsLog()
{
    return dynamic_cast<QwtLogScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yLeft));
}

bool Histo1DWidget::yAxisIsLin()
{
    return dynamic_cast<QwtLinearScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yLeft));
}

void Histo1DWidget::exportPlot()
{
    QString fileName = m_histo->objectName();
    QwtPlotRenderer renderer;
    renderer.exportTo(ui->plot, fileName);
}
void Histo1DWidget::saveHistogram()
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

    fileName = QFileDialog::getSaveFileName(this, "Save Histogram", fileName, "Text Files (*.histo1d);; All Files (*.*)");

    if (fileName.isEmpty())
        return;

    QFileInfo fi(fileName);
    if (fi.completeSuffix().isEmpty())
    {
        fileName += ".histo1d";
    }

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return;
    }

    QTextStream out(&outFile);
    writeHisto1D(out, m_histo);

    if (out.status() == QTextStream::Ok)
    {
        fi.setFile(fileName);
        QSettings().setValue("Files/LastHistogramExportDirectory", fi.absolutePath());
    }
}

void Histo1DWidget::updateCursorInfoLabel()
{
    double plotX = m_cursorPosition.x();
    double plotY = m_cursorPosition.y();
    auto binning = m_histo->getAxisBinning(Qt::XAxis);
    s64 binX = binning.getBin(plotX);

    QString text;

    if (!qIsNaN(plotX) && !qIsNaN(plotY) && binX >= 0)
    {
        double x = plotX;
        double y = m_histo->getBinContent(binX);
        double binLowEdge = binning.getBinLowEdge(binX);

        text = QString("x=%1\n"
                       "y=%2\n"
                       "bin=%3\n"
                       "low edge=%4"
                      )
            .arg(x)
            .arg(y)
            .arg(binX)
            .arg(binLowEdge)
            ;
#if 0
        double binXUnchecked = binning.getBinUnchecked(plotX);

        auto sl = QStringList()
            << QString("cursorPlotX=%1").arg(plotX)
            << QString("binXUnchecked=%1").arg(binXUnchecked)
            << QString("binX=%1, u=%2, o=%3").arg(binX).arg(binX == AxisBinning::Underflow).arg(binX == AxisBinning::Overflow)
            << QString("nBins=%1").arg(binning.getBins())
            << QString("binXLow=%1").arg(binning.getBinLowEdge(binX))
            << QString("binXCenter=%1").arg(binning.getBinCenter(binX))
            << QString("binWidth=%1").arg(binning.getBinWidth())
            << QString("Y=%1").arg(m_histo->getBinContent(binX))
            << QString("minX=%1, maxX=%2").arg(m_histo->getXMin()).arg(m_histo->getXMax());
        ;

        QString text = sl.join("\n");
#endif
    }

    // update the label which will calculate a new width
    ui->label_cursorInfo->setText(text);
    // use the largest width the label ever had to stop the label from constantly changing its width
    m_labelCursorInfoWidth = std::max(m_labelCursorInfoWidth, ui->label_cursorInfo->width());
    ui->label_cursorInfo->setMinimumWidth(m_labelCursorInfoWidth);
}

void Histo1DWidget::setCalibrationInfo(const std::shared_ptr<analysis::CalibrationMinMax> &calib, s32 histoAddress, MVMEContext *context)
{
    m_calib = calib;
    m_histoAddress = histoAddress;
    ui->frame_calib->setVisible(m_calib != nullptr);
    m_context = context;
}

void Histo1DWidget::calibApply()
{
    Q_ASSERT(m_calib);
    Q_ASSERT(m_context);

    double a1 = m_calibUi->actual1->value();
    double a2 = m_calibUi->actual2->value();
    double t1 = m_calibUi->target1->value();
    double t2 = m_calibUi->target2->value();

    if (a1 - a2 == 0.0 || t1 == t2)
        return;

    double a = (t1 - t2) / (a1 - a2);
    double b = t1 - a * a1;

    u32 address = m_histoAddress;

    double actualMin = m_calib->getCalibration(address).unitMin;
    double actualMax = m_calib->getCalibration(address).unitMax;

    double targetMin = a * actualMin + b;
    double targetMax = a * actualMax + b;

    qDebug() << __PRETTY_FUNCTION__ << endl
        << "address" << address << endl
        << "a1 a2" << a1 << a2 << endl
        << "t1 t2" << t1 << t2 << endl
        << "aMinMax" << actualMin << actualMax << endl
        << "tMinMax" << targetMin << targetMax;

    m_calibUi->actual1->setValue(m_calibUi->target1->value());
    m_calibUi->actual2->setValue(m_calibUi->target2->value());

    AnalysisPauser pauser(m_context);
    m_calib->setCalibration(address, targetMin, targetMax);
    analysis::do_beginRun_forward(m_calib.get());
}

void Histo1DWidget::calibResetToFilter()
{
    u32 address = m_histoAddress;
    auto globalCalib = m_calib->getGlobalCalibration();
    AnalysisPauser pauser(m_context);
    m_calib->setCalibration(address, globalCalib);
    analysis::do_beginRun_forward(m_calib.get());
}

void Histo1DWidget::calibFillMax()
{
    double maxAt = m_histo->getAxisBinning(Qt::XAxis).getBinCenter(m_stats.maxBin);
    m_calibUi->lastFocusedActual->setValue(maxAt);
}

bool Histo1DWidget::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_calibUi->actual1 || watched == m_calibUi->actual2)
        && (event->type() == QEvent::FocusIn))
    {
        m_calibUi->lastFocusedActual = qobject_cast<QDoubleSpinBox *>(watched);
    }
    return QWidget::eventFilter(watched, event);
}

//
// Histo1DListWidget
//
Histo1DListWidget::Histo1DListWidget(const HistoList &histos, QWidget *parent)
    : QWidget(parent)
    , m_histos(histos)
    , m_currentIndex(0)
{
    Q_ASSERT(histos.size());

    auto histo = histos[0].get();
    m_histoWidget = new Histo1DWidget(histo, this);

    connect(m_histoWidget, &QWidget::windowTitleChanged, this, &QWidget::setWindowTitle);

    /* create the controls to switch the current histogram and inject into the
     * histo widget layout. */
    auto gb = new QGroupBox(QSL("Histogram"));
    auto histoSpinLayout = new QHBoxLayout(gb);
    histoSpinLayout->setContentsMargins(0, 0, 0, 0);

    auto histoSpinBox = new QSpinBox;
    histoSpinBox->setMaximum(histos.size() - 1);
    connect(histoSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &Histo1DListWidget::onHistoSpinBoxValueChanged);

    histoSpinLayout->addWidget(histoSpinBox);

    auto controlsLayout = m_histoWidget->ui->controlsLayout;
    controlsLayout->insertWidget(0, gb);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_histoWidget);

    setWindowTitle(m_histoWidget->windowTitle());
    onHistoSpinBoxValueChanged(0);
}

void Histo1DListWidget::onHistoSpinBoxValueChanged(int index)
{
    m_currentIndex = index;
    auto histo = m_histos.value(index);

    if (histo)
    {
        m_histoWidget->setHistogram(histo.get());
        if (m_calib)
        {
            m_histoWidget->setCalibrationInfo(m_calib, index, m_context);
        }
    }
}

void Histo1DListWidget::setCalibration(const std::shared_ptr<analysis::CalibrationMinMax> &calib, MVMEContext *context)
{
    m_calib = calib;
    m_context = context;
    if (m_calib)
    {
        m_histoWidget->setCalibrationInfo(m_calib, m_currentIndex, m_context);
    }
}
