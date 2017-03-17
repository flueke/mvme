#include "histo2d_widget.h"
#include "ui_histo2d_widget.h"
#include "histo2d_widget_p.h"
#include "scrollzoomer.h"
#include "util.h"
#include "analysis/analysis.h"

#include <qwt_plot_spectrogram.h>
#include <qwt_color_map.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_raster_data.h>
#include <qwt_scale_engine.h>

#include <QDebug>
#include <QTimer>

static const s32 ReplotPeriod_ms = 1000;

class Histo2DRasterData: public QwtRasterData
{
public:

    Histo2DRasterData(Histo2D *histo)
        : m_histo(histo)
    {
        updateIntervals();
    }

    virtual double value(double x, double y) const
    {
        double v = m_histo->getValue(x, y);
        return (v > 0.0 ? v : make_quiet_nan());
    }

    void updateIntervals()
    {
        for (int axis=0; axis<3; ++axis)
        {
            AxisInterval interval = m_histo->getInterval(static_cast<Qt::Axis>(axis));
            setInterval(static_cast<Qt::Axis>(axis), QwtInterval(interval.minValue, interval.maxValue));
        }
    }

private:
    Histo2D *m_histo;
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

Histo2DWidget::Histo2DWidget(const Histo2DPtr histoPtr, QWidget *parent)
    : Histo2DWidget(histoPtr.get(), parent)
{
    m_histoPtr = histoPtr;
}

Histo2DWidget::Histo2DWidget(Histo2D *histo, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Histo2DWidget)
    , m_histo(histo)
    , m_plotItem(new QwtPlotSpectrogram)
    , m_replotTimer(new QTimer(this))
    , m_cursorPosition(make_quiet_nan(), make_quiet_nan())
    , m_labelCursorInfoWidth(-1)
{
    ui->setupUi(this);

    ui->tb_info->setEnabled(false);
    ui->tb_subRange->setEnabled(false);

    connect(ui->pb_export, &QPushButton::clicked, this, &Histo2DWidget::exportPlot);

    connect(ui->pb_clear, &QPushButton::clicked, this, [this] {
        m_histo->clear();
        replot();
    });

    connect(ui->linLogGroup, SIGNAL(buttonClicked(int)), this, SLOT(displayChanged()));

    auto histData = new Histo2DRasterData(m_histo);
    m_plotItem->setData(histData);
    m_plotItem->setRenderThreadCount(0); // use system specific ideal thread count
    m_plotItem->setColorMap(getColorMap());
    m_plotItem->attach(ui->plot);

    auto rightAxis = ui->plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Counts");
    rightAxis->setColorBarEnabled(true);
    ui->plot->enableAxis(QwtPlot::yRight);

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(ReplotPeriod_ms);

    ui->plot->canvas()->setMouseTracking(true);

    m_zoomer = new ScrollZoomer(ui->plot->canvas());
    connect(m_zoomer, &ScrollZoomer::zoomed, this, &Histo2DWidget::zoomerZoomed);
    connect(m_zoomer, &ScrollZoomer::mouseCursorMovedTo, this, &Histo2DWidget::mouseCursorMovedToPlotCoord);
    connect(m_zoomer, &ScrollZoomer::mouseCursorLeftPlot, this, &Histo2DWidget::mouseCursorLeftPlot);

    connect(m_histo, &Histo2D::axisBinningChanged, this, [this] (Qt::Axis) {
        // Handle axis changes by zooming out fully. This will make sure
        // possible axis scale changes are immediately visible and the zoomer
        // is in a clean state.
        m_zoomer->setZoomStack(QStack<QRectF>(), -1);
        m_zoomer->zoom(0);
        replot();
    });


#if 0
    auto plotPanner = new QwtPlotPanner(ui->plot->canvas());
    plotPanner->setMouseButton(Qt::MiddleButton);

    auto plotMagnifier = new QwtPlotMagnifier(ui->plot->canvas());
    plotMagnifier->setMouseButton(Qt::NoButton);
#endif

    displayChanged();
}

Histo2DWidget::~Histo2DWidget()
{
    delete m_plotItem;
    delete ui;
}

void Histo2DWidget::replot()
{
    auto histData = reinterpret_cast<Histo2DRasterData *>(m_plotItem->data());
    histData->updateIntervals();

    if (m_zoomer->zoomRectIndex() == 0)
    {
        // Fully zoomed out => set axis scales to full size and use that as the zoomer base.

        auto xInterval = histData->interval(Qt::XAxis);
        ui->plot->setAxisScale(QwtPlot::xBottom, xInterval.minValue(), xInterval.maxValue());

        auto yInterval = histData->interval(Qt::YAxis);
        ui->plot->setAxisScale(QwtPlot::yLeft, yInterval.minValue(), yInterval.maxValue());

        m_zoomer->setZoomBase();
    }

    // z axis interval
    auto interval = histData->interval(Qt::ZAxis);
    double base = zAxisIsLog() ? 1.0 : 0.0;
    interval = interval.limited(base, interval.maxValue());

    ui->plot->setAxisScale(QwtPlot::yRight, interval.minValue(), interval.maxValue());
    auto axis = ui->plot->axisWidget(QwtPlot::yRight);
    axis->setColorMap(interval, getColorMap());

    // window and axis titles
    auto name = m_histo->objectName();
    setWindowTitle(QString("Histogram %1").arg(name));

    {
        auto axisInfo = m_histo->getAxisInfo(Qt::XAxis);
        ui->plot->axisWidget(QwtPlot::xBottom)->setTitle(make_title_string(axisInfo));
    }

    {
        auto axisInfo = m_histo->getAxisInfo(Qt::YAxis);
        ui->plot->axisWidget(QwtPlot::yLeft)->setTitle(make_title_string(axisInfo));
    }


    // stats display
    QwtInterval xInterval = ui->plot->axisScaleDiv(QwtPlot::xBottom).interval();
    QwtInterval yInterval = ui->plot->axisScaleDiv(QwtPlot::yLeft).interval();

    auto stats = m_histo->calcStatistics(
        {xInterval.minValue(), xInterval.maxValue()},
        {yInterval.minValue(), yInterval.maxValue()});

    double maxX = stats.maxX;
    double maxY = stats.maxY;

    ui->label_numberOfEntries->setText(QString("%L1").arg(stats.entryCount));
    ui->label_maxValue->setText(QString("%L1").arg(stats.maxValue));

    ui->label_maxX->setText(QString("%1").arg(maxX, 0, 'g', 6));
    ui->label_maxY->setText(QString("%1").arg(maxY, 0, 'g', 6));

    updateCursorInfoLabel();

    // update histo info label
    auto infoText = QString("Underflow: %1\n"
                            "Overflow:  %2")
        .arg(m_histo->getUnderflow())
        .arg(m_histo->getOverflow());
    ui->label_histoInfo->setText(infoText);

    ui->plot->replot();
}

void Histo2DWidget::displayChanged()
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

    replot();
}

void Histo2DWidget::exportPlot()
{
    QString fileName = m_histo->objectName();
    QwtPlotRenderer renderer;
    renderer.exportTo(ui->plot, fileName);
}

bool Histo2DWidget::zAxisIsLog() const
{
    return dynamic_cast<QwtLogScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yRight));
}

bool Histo2DWidget::zAxisIsLin() const
{
    return dynamic_cast<QwtLinearScaleEngine *>(ui->plot->axisScaleEngine(QwtPlot::yRight));
}

QwtLinearColorMap *Histo2DWidget::getColorMap() const
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

void Histo2DWidget::mouseCursorMovedToPlotCoord(QPointF pos)
{
    m_cursorPosition = pos;
    updateCursorInfoLabel();
}

void Histo2DWidget::mouseCursorLeftPlot()
{
    m_cursorPosition = QPointF(make_quiet_nan(), make_quiet_nan());
    updateCursorInfoLabel();
}

void Histo2DWidget::zoomerZoomed(const QRectF &zoomRect)
{
#if 0
    // do not zoom into negatives or above the upper bin

    // x
    auto scaleDiv = ui->plot->axisScaleDiv(QwtPlot::xBottom);
    auto maxValue = m_histo->interval(Qt::XAxis).maxValue();

    if (scaleDiv.lowerBound() < 0.0)
        scaleDiv.setLowerBound(0.0);

    if (scaleDiv.upperBound() > maxValue)
        scaleDiv.setUpperBound(maxValue);

    ui->plot->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);

    // y
    scaleDiv = ui->plot->axisScaleDiv(QwtPlot::yLeft);
    maxValue = m_histo->interval(Qt::YAxis).maxValue();

    if (scaleDiv.lowerBound() < 0.0)
        scaleDiv.setLowerBound(0.0);

    if (scaleDiv.upperBound() > maxValue)
        scaleDiv.setUpperBound(maxValue);

    ui->plot->setAxisScaleDiv(QwtPlot::yLeft, scaleDiv);
    replot();
#endif
}

void Histo2DWidget::updateCursorInfoLabel()
{
    double plotX = m_cursorPosition.x();
    double plotY = m_cursorPosition.y();
    s64 binX = m_histo->getAxisBinning(Qt::XAxis).getBin(plotX);
    s64 binY = m_histo->getAxisBinning(Qt::YAxis).getBin(plotY);

    QString text;

    if (!qIsNaN(plotX) && !qIsNaN(plotY) && binX >= 0 && binY >= 0)
    {
        double value = m_histo->getValue(plotX, plotY);

        if (qIsNaN(value))
            value = 0.0;

        text = QString("x=%1\n"
                       "y=%2\n"
                       "z=%3\n"
                       "xbin=%4\n"
                       "ybin=%5"
                      )
            .arg(plotX, 0, 'g', 6)
            .arg(plotY, 0, 'g', 6)
            .arg(value)
            .arg(binX)
            .arg(binY)
            ;

    }

    // update the label which will calculate a new width
    ui->label_cursorInfo->setText(text);
    // use the largest width the label ever had to stop the label from constantly changing its width
    m_labelCursorInfoWidth = std::max(m_labelCursorInfoWidth, ui->label_cursorInfo->width());
    ui->label_cursorInfo->setMinimumWidth(m_labelCursorInfoWidth);
}

void Histo2DWidget::on_tb_info_clicked()
{
#if 0
    auto histoConfig = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_histo, QSL("ObjectToConfig")));

    if (!histoConfig) return;

    auto dialogPtr = new QDialog(this);
    auto &dialog = *dialogPtr;
    dialog.setWindowTitle("Histogram Information");


    QFormLayout layout(&dialog);

    auto label = new QLabel(m_histo->objectName());
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
#endif
}

void Histo2DWidget::setSink(const SinkPtr &sink, HistoSinkCallback addSinkCallback, HistoSinkCallback sinkModifiedCallback)
{
    Q_ASSERT(sink && sink->m_histo.get() == m_histo);
    m_sink = sink;
    m_addSinkCallback = addSinkCallback;
    m_sinkModifiedCallback = sinkModifiedCallback;
    ui->tb_subRange->setEnabled(true);
}

void Histo2DWidget::on_tb_subRange_clicked()
{
    Q_ASSERT(m_sink);
    double visibleMinX = ui->plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = ui->plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    double visibleMinY = ui->plot->axisScaleDiv(QwtPlot::yLeft).lowerBound();
    double visibleMaxY = ui->plot->axisScaleDiv(QwtPlot::yLeft).upperBound();
    Histo2DSubRangeDialog dialog(m_sink, m_addSinkCallback, m_sinkModifiedCallback,
                                 visibleMinX, visibleMaxX, visibleMinY, visibleMaxY,
                                 this);
    dialog.exec();
}
