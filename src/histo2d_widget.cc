/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "histo2d_widget.h"
#include "histo2d_widget_p.h"

#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "histo1d_widget.h"
#include "mvme_context.h"
#include "scrollzoomer.h"
#include "util.h"

#include <qwt_plot_spectrogram.h>
#include <qwt_color_map.h>
#include <qwt_scale_widget.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_textlabel.h>
#include <qwt_raster_data.h>
#include <qwt_scale_engine.h>

#include <QBoxLayout>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFormLayout>
#include <QStatusBar>
#include <QStatusTipEvent>
#include <QTimer>
#include <QToolBar>

#ifdef MVME_USE_GIT_VERSION_FILE
#include "git_sha1.h"
#endif

static const s32 ReplotPeriod_ms = 1000;

using Intervals = Histo2DStatistics::Intervals;

class RasterDataBase: public QwtRasterData
{
public:
    void setIntervals(const Intervals &intervals)
    {
        for (size_t axis = 0; axis < intervals.size(); axis++)
        {
            setInterval(static_cast<Qt::Axis>(axis),
                        { intervals[axis].minValue, intervals[axis].maxValue });

            //qDebug() << __PRETTY_FUNCTION__ << "axis =" << axis
            //    << ", min =" << intervals[axis].minValue
            //    << ", max =" << intervals[axis].maxValue;
        }
    }
};

struct Histo2DRasterData: public RasterDataBase
{
    Histo2DRasterData(Histo2D *histo)
        : RasterDataBase()
        , m_histo(histo)
    {
    }

    virtual double value(double x, double y) const override
    {
        double v = m_histo->getValue(x, y);
        double r = (v > 0.0 ? v : make_quiet_nan());
        return r;
    }

    Histo2D *m_histo;
};

using HistoList = QVector<std::shared_ptr<Histo1D>>;

struct Histo1DListRasterData: public RasterDataBase
{
    Histo1DListRasterData(const HistoList &histos)
        : RasterDataBase()
        , m_histos(histos)
    {
    }

    virtual double value(double x, double y) const override
    {
        int histoIndex = x;

        if (histoIndex < 0 || histoIndex >= m_histos.size())
            return make_quiet_nan();

        double v = m_histos[histoIndex]->getValue(y);
        double r = (v > 0.0 ? v : make_quiet_nan());
        return r;
    }

    HistoList m_histos;
};

using Histo1DSinkPtr = Histo2DWidget::Histo1DSinkPtr;

static Histo2DStatistics calc_Histo1DSink_combined_stats(const Histo1DSinkPtr &sink,
                                                         AxisInterval xInterval,
                                                         AxisInterval yInterval,
                                                         analysis::A2AdapterState *a2State)
{
    assert(sink);
    assert(a2State);
    Histo2DStatistics result;

    /* Counts: sum of all histo counts
     * Max Z: absolute max value of the histos
     * Coordinates: x = histo#, y = x coordinate of the max value in the histo
     *
     * Note: this solution is not perfect. The zoom level is not taken into
     * account. Also histo counts remain after the histos have been cleared
     * via "Clear Histograms". Upon starting a new run the counts are ok
     * again.
     */

    s32 firstHistoIndex = std::max(xInterval.minValue, 0.0);
    s32 lastHistoIndex  = std::min(xInterval.maxValue, static_cast<double>(sink->m_histos.size() - 1));

    for (s32 hi = firstHistoIndex; hi < lastHistoIndex; hi++)
    {
        auto histo  = sink->m_histos.at(hi);
        auto stats = histo->calcStatistics(yInterval.minValue, yInterval.maxValue);

        result.entryCount += stats.entryCount;

        if (stats.maxValue > result.maxZ)
        {
            result.maxBinX = hi;
            result.maxBinY = stats.maxBin;
            result.maxX = hi;
            result.maxY = histo->getBinLowEdge(stats.maxBin);
            result.maxZ = stats.maxValue;
        }
    }

    auto firstHisto   = sink->m_histos.at(firstHistoIndex);
    auto firstBinning = firstHisto->getAxisBinning(Qt::XAxis);

    result.intervals[Qt::XAxis] = { static_cast<double>(firstHistoIndex), static_cast<double>(lastHistoIndex) };
    result.intervals[Qt::YAxis] = { firstBinning.getMin(), firstBinning.getMax() };
    result.intervals[Qt::ZAxis] = { 0.0, result.maxZ };

    return result;
}

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

struct Histo2DWidgetPrivate
{
    Histo2DWidget *m_q;

    QToolBar *m_toolBar;
    QwtPlot *m_plot;
    QStatusBar *m_statusBar;

    QLabel *m_labelCursorInfo;
    QLabel *m_labelHistoInfo;
    QWidget *m_infoContainer;

    s32 m_labelCursorInfoMaxWidth  = 0;
    s32 m_labelCursorInfoMaxHeight = 0;

    QAction *m_actionClear,
            *m_actionSubRange,
            *m_actionChangeRes,
            *m_actionInfo;

    QComboBox *m_zScaleCombo;

    QwtText *m_waterMarkText;
    QwtPlotTextLabel *m_waterMarkLabel;

    void onActionChangeResolution()
    {
        auto combo_xBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);
        auto combo_yBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);

        select_by_resolution(combo_xBins, m_q->m_sink->m_xBins);
        select_by_resolution(combo_yBins, m_q->m_sink->m_yBins);

        QDialog dialog(m_q);
        auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        auto layout = new QFormLayout(&dialog);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->addRow(QSL("X Resolution"), combo_xBins);
        layout->addRow(QSL("Y Resolution"), combo_yBins);
        layout->addRow(buttonBox);

        if (dialog.exec() == QDialog::Accepted)
        {
            auto sink  = m_q->m_sink;
            auto xBins = combo_xBins->currentData().toInt();
            auto yBins = combo_yBins->currentData().toInt();

            bool modified = (sink->m_xBins != xBins || sink->m_yBins != yBins);

            sink->m_xBins = xBins;
            sink->m_yBins = yBins;

            if (modified)
            {
                m_q->m_context->analysisOperatorEdited(sink);
            }
        }
    }
};

enum class AxisScaleType
{
    Linear,
    Logarithmic
};

static QWidget *make_spacer_widget()
{
    auto result = new QWidget;
    result->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return result;
}

Histo2DWidget::Histo2DWidget(const Histo2DPtr histoPtr, QWidget *parent)
    : Histo2DWidget(histoPtr.get(), parent)
{
    m_histoPtr = histoPtr;
}

Histo2DWidget::Histo2DWidget(Histo2D *histo, QWidget *parent)
    : Histo2DWidget(parent)
{
    m_histo = histo;
    auto histData = new Histo2DRasterData(m_histo);
    m_plotItem->setData(histData);

    connect(m_histo, &Histo2D::axisBinningChanged, this, [this] (Qt::Axis) {
        // Handle axis changes by zooming out fully. This will make sure
        // possible axis scale changes are immediately visible and the zoomer
        // is in a clean state.
        m_zoomer->setZoomStack(QStack<QRectF>(), -1);
        m_zoomer->zoom(0);
        replot();
    });

    connect(m_d->m_actionClear, &QAction::triggered, this, [this]() {
        m_histo->clear();
        replot();
    });

    displayChanged();
}

Histo2DWidget::Histo2DWidget(const Histo1DSinkPtr &histo1DSink, MVMEContext *context, QWidget *parent)
    : Histo2DWidget(parent)
{
    Q_ASSERT(histo1DSink);
    Q_ASSERT(context);

    m_context = context;
    m_histo1DSink = histo1DSink;
    auto histData = new Histo1DListRasterData(m_histo1DSink->m_histos);
    m_plotItem->setData(histData);

    connect(m_d->m_actionClear, &QAction::triggered, this, [this]() {
        for (auto &histo: m_histo1DSink->m_histos)
        {
            histo->clear();
        }
        replot();
    });

    displayChanged();
}

Histo2DWidget::Histo2DWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(new Histo2DWidgetPrivate)
    , m_plotItem(new QwtPlotSpectrogram)
    , m_replotTimer(new QTimer(this))
    , m_cursorPosition(make_quiet_nan(), make_quiet_nan())
    , m_labelCursorInfoWidth(-1)
    , m_geometrySaver(new WidgetGeometrySaver(this))
{
    m_d->m_q = this;

    m_d->m_toolBar = new QToolBar;
    m_d->m_plot = new QwtPlot;
    m_d->m_statusBar = make_statusbar();

    // Toolbar and actions
    auto tb = m_d->m_toolBar;
    {
        tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tb->setIconSize(QSize(16, 16));
        set_widget_font_pointsize(tb, 7);
    }

    QAction *action = nullptr;

    // Z-Scale Selection
    {
        m_d->m_zScaleCombo = new QComboBox;
        auto zScaleCombo = m_d->m_zScaleCombo;

        zScaleCombo->addItem(QSL("Lin"), static_cast<int>(AxisScaleType::Linear));
        zScaleCombo->addItem(QSL("Log"), static_cast<int>(AxisScaleType::Logarithmic));

        connect(zScaleCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                this, &Histo2DWidget::displayChanged);

        tb->addWidget(make_vbox_container(QSL("Z-Scale"), zScaleCombo));
    }

    action = tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("X-Proj"), this, &Histo2DWidget::on_tb_projX_clicked);
    action = tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("Y-Proj"), this, &Histo2DWidget::on_tb_projY_clicked);
    m_d->m_actionClear = tb->addAction(QIcon(":/clear_histos.png"), QSL("Clear")); // Connected by other constructors

    action = tb->addAction(QIcon(":/document-pdf.png"), QSL("Export"), this, &Histo2DWidget::exportPlot);
    action->setStatusTip(QSL("Export plot to a PDF or image file"));

    m_d->m_actionSubRange = tb->addAction(QIcon(":/histo_subrange.png"), QSL("Subrange"), this, &Histo2DWidget::on_tb_subRange_clicked);
    m_d->m_actionSubRange->setStatusTip(QSL("Limit the histogram to specific X and Y axis ranges"));
    m_d->m_actionSubRange->setEnabled(false);

    m_d->m_actionChangeRes = tb->addAction(QIcon(":/histo_resolution.png"), QSL("Resolution"),
                                           this, [this]() { m_d->onActionChangeResolution(); });
    m_d->m_actionChangeRes->setStatusTip(QSL("Change histogram resolution"));
    m_d->m_actionChangeRes->setEnabled(false);

    m_d->m_actionInfo = tb->addAction(QIcon(":/info.png"), QSL("Info"));
    m_d->m_actionInfo->setCheckable(true);
    connect(m_d->m_actionInfo, &QAction::toggled, this, [this](bool b) {
        for (auto childWidget: m_d->m_infoContainer->findChildren<QWidget *>())
        {
            childWidget->setVisible(b);
        }
    });

    // Plot

    m_plotItem->setRenderThreadCount(0); // use system specific ideal thread count
    m_plotItem->setColorMap(getColorMap());
    m_plotItem->attach(m_d->m_plot);

    auto rightAxis = m_d->m_plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Counts");
    rightAxis->setColorBarEnabled(true);
    m_d->m_plot->enableAxis(QwtPlot::yRight);

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(ReplotPeriod_ms);

    m_d->m_plot->canvas()->setMouseTracking(true);

    m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());

    TRY_ASSERT(connect(m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));

    TRY_ASSERT(connect(m_zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &Histo2DWidget::mouseCursorMovedToPlotCoord));

    TRY_ASSERT(connect(m_zoomer, &ScrollZoomer::mouseCursorLeftPlot,
                       this, &Histo2DWidget::mouseCursorLeftPlot));

    //
    // Watermark text when exporting
    //
    {
        m_d->m_waterMarkText = new QwtText;
        m_d->m_waterMarkText->setRenderFlags(Qt::AlignRight | Qt::AlignBottom);
        m_d->m_waterMarkText->setColor(QColor(0x66, 0x66, 0x66, 0x40));

        QFont font;
        font.setPixelSize(16);
        font.setBold(true);
        m_d->m_waterMarkText->setFont(font);

        m_d->m_waterMarkText->setText(QString("mvme-%1").arg(GIT_VERSION_TAG));

        m_d->m_waterMarkLabel = new QwtPlotTextLabel;
        m_d->m_waterMarkLabel->setMargin(10);
        m_d->m_waterMarkLabel->setText(*m_d->m_waterMarkText);
        m_d->m_waterMarkLabel->attach(m_d->m_plot);
        m_d->m_waterMarkLabel->hide();
    }

    // Info widgets
    m_d->m_labelCursorInfo = new QLabel;
    m_d->m_labelHistoInfo = new QLabel;

    for (auto label: { m_d->m_labelCursorInfo, m_d->m_labelHistoInfo})
    {
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        set_widget_font_pointsize(label, 7);
    }

    {
        m_d->m_infoContainer = new QWidget;

        auto layout = new QHBoxLayout(m_d->m_infoContainer);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(1);
        layout->addWidget(m_d->m_labelCursorInfo);
        layout->addWidget(m_d->m_labelHistoInfo);

        for (auto childWidget: m_d->m_infoContainer->findChildren<QWidget *>())
        {
            childWidget->setVisible(false);
        }

        m_d->m_statusBar->addPermanentWidget(m_d->m_infoContainer);
    }

    // Main Widget Layout

    auto toolBarFrame = new QFrame;
    toolBarFrame->setFrameStyle(QFrame::StyledPanel);
    {
        auto layout = new QHBoxLayout(toolBarFrame);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_d->m_toolBar);
    }

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);
    mainLayout->addWidget(toolBarFrame);
    mainLayout->addWidget(m_d->m_plot);
    mainLayout->addWidget(m_d->m_statusBar);
    mainLayout->setStretch(1, 1);
}

Histo2DWidget::~Histo2DWidget()
{
    delete m_plotItem;
    delete m_d;
}

void Histo2DWidget::replot()
{
    /* Things that have to happen:
     * - calculate stats for the visible area. use this to scale z
     * - update info display
     * - update cursor info
     * - update axis titles
     * - update window title
     * - update projections
     */

    QwtInterval visibleXInterval = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).interval();
    QwtInterval visibleYInterval = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).interval();

    // If fully zoomed out set axis scales to full size and use that as the zoomer base.
    if (m_zoomer->zoomRectIndex() == 0)
    {
        if (m_histo)
        {
            visibleXInterval =
            {
                m_histo->getAxisBinning(Qt::XAxis).getMin(),
                m_histo->getAxisBinning(Qt::XAxis).getMax()
            };

            visibleYInterval =
            {
                m_histo->getAxisBinning(Qt::YAxis).getMin(),
                m_histo->getAxisBinning(Qt::YAxis).getMax()
            };
        }
        else if (m_histo1DSink)
        {
            auto firstHisto   = m_histo1DSink->m_histos.at(0);
            auto firstBinning = firstHisto->getAxisBinning(Qt::XAxis);

            visibleXInterval =
            {
                0.0,
                static_cast<double>(m_histo1DSink->m_histos.size() - 1)
            };

            visibleYInterval =
            {
                firstBinning.getMin(),
                firstBinning.getMax(),
            };
        }

        m_d->m_plot->setAxisScale(QwtPlot::xBottom, visibleXInterval.minValue(), visibleXInterval.maxValue());
        m_d->m_plot->setAxisScale(QwtPlot::yLeft,   visibleYInterval.minValue(), visibleYInterval.maxValue());
        m_zoomer->setZoomBase();
    }

    Histo2DStatistics stats;

    if (m_histo)
    {
        stats = m_histo->calcStatistics(
            { visibleXInterval.minValue(), visibleXInterval.maxValue() },
            { visibleYInterval.minValue(), visibleYInterval.maxValue() });
    }
    else if (m_histo1DSink)
    {
        stats = calc_Histo1DSink_combined_stats(
            m_histo1DSink,
            { visibleXInterval.minValue(), visibleXInterval.maxValue() },
            { visibleYInterval.minValue(), visibleYInterval.maxValue() },
            m_context->getAnalysis()->getA2AdapterState());
    }

    auto rasterData = reinterpret_cast<RasterDataBase *>(m_plotItem->data());
    rasterData->setIntervals(stats.intervals);

    auto zInterval = rasterData->interval(Qt::ZAxis);
    double zBase = zAxisIsLog() ? 1.0 : 0.0;
    zInterval = zInterval.limited(zBase, zInterval.maxValue());

    m_d->m_plot->setAxisScale(QwtPlot::yRight, zInterval.minValue(), zInterval.maxValue());

    auto axis = m_d->m_plot->axisWidget(QwtPlot::yRight);
    axis->setColorMap(zInterval, getColorMap());

    // cursor info
    updateCursorInfoLabel();

    // window and axis titles
    QString windowTitle;
    if (m_histo)
    {
        windowTitle = QString("Histogram %1").arg(m_histo->objectName());

        auto axisInfo = m_histo->getAxisInfo(Qt::XAxis);
        m_d->m_plot->axisWidget(QwtPlot::xBottom)->setTitle(make_title_string(axisInfo));

        axisInfo = m_histo->getAxisInfo(Qt::YAxis);
        m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle(make_title_string(axisInfo));
    }
    else if (m_histo1DSink)
    {
        windowTitle = QString("%1 2D combined").arg(m_histo1DSink->objectName());

        m_d->m_plot->axisWidget(QwtPlot::xBottom)->setTitle(QSL("Histogram #"));

        // Use the first histograms x axis as the title for the combined y axis
        if (auto histo = m_histo1DSink->getHisto(0))
        {
            auto axisInfo = histo->getAxisInfo(Qt::XAxis);
            m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle(make_title_string(axisInfo));
        }
    }
    else
    {
        InvalidCodePath;
    }

    setWindowTitle(windowTitle);

    // stats display
    auto infoText = (QString(
            "Counts: %1\n"
            "Max Z:  %2 @ (%3, %4)\n")
        .arg(stats.entryCount)
        .arg(stats.maxZ)
        .arg(stats.maxX, 0, 'g', 6)
        .arg(stats.maxY, 0, 'g', 6)
        );

    m_d->m_labelHistoInfo->setText(infoText);

    // tell qwt to replot
    m_d->m_plot->replot();

    // projections
    if (m_xProjWidget)
    {
        doXProjection();
    }

    if (m_yProjWidget)
    {
        doYProjection();
    }
}

void Histo2DWidget::setLinZ()
{
    m_d->m_zScaleCombo->setCurrentIndex(0);
}

void Histo2DWidget::setLogZ()
{
    m_d->m_zScaleCombo->setCurrentIndex(1);
}

void Histo2DWidget::displayChanged()
{
    auto scaleType = static_cast<AxisScaleType>(m_d->m_zScaleCombo->currentData().toInt());

    if (scaleType == AxisScaleType::Linear && !zAxisIsLin())
    {
        m_d->m_plot->setAxisScaleEngine(QwtPlot::yRight, new QwtLinearScaleEngine);
        m_d->m_plot->setAxisAutoScale(QwtPlot::yRight, true);
    }
    else if (scaleType == AxisScaleType::Logarithmic && !zAxisIsLog())
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        m_d->m_plot->setAxisScaleEngine(QwtPlot::yRight, scaleEngine);
        m_d->m_plot->setAxisAutoScale(QwtPlot::yRight, true);
    }

    m_plotItem->setColorMap(getColorMap());

    replot();
}

void Histo2DWidget::exportPlot()
{
    QString fileName;
    QString title;
    QString footer;

    if (m_histo)
    {
        fileName = m_histo->objectName();
        title = m_histo->getTitle();
        footer = m_histo->getFooter();
    }
    else if (m_histo1DSink)
    {
        fileName = m_histo1DSink->objectName();
        title = windowTitle();
        // just use the first histograms footer
        if (auto h1d = m_histo1DSink->m_histos.value(0))
        {
            footer = h1d->getFooter();
        }
    }
    else
    {
        InvalidCodePath;
    }

    fileName.replace("/", "_");
    fileName.replace("\\", "_");
    fileName += QSL(".pdf");

    if (m_context)
    {
        fileName = QDir(m_context->getWorkspacePath(QSL("PlotsDirectory"))).filePath(fileName);
    }

    m_d->m_plot->setTitle(title);


    QwtText footerText(footer);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_d->m_plot->setFooter(footerText);
    m_d->m_waterMarkLabel->show();

    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.exportTo(m_d->m_plot, fileName);

    m_d->m_plot->setTitle(QString());
    m_d->m_plot->setFooter(QString());
    m_d->m_waterMarkLabel->hide();
}

bool Histo2DWidget::zAxisIsLog() const
{
    return dynamic_cast<QwtLogScaleEngine *>(m_d->m_plot->axisScaleEngine(QwtPlot::yRight));
}

bool Histo2DWidget::zAxisIsLin() const
{
    return dynamic_cast<QwtLinearScaleEngine *>(m_d->m_plot->axisScaleEngine(QwtPlot::yRight));
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
    auto scaleDiv = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom);
    auto maxValue = m_histo->interval(Qt::XAxis).maxValue();

    if (scaleDiv.lowerBound() < 0.0)
        scaleDiv.setLowerBound(0.0);

    if (scaleDiv.upperBound() > maxValue)
        scaleDiv.setUpperBound(maxValue);

    m_d->m_plot->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);

    // y
    scaleDiv = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft);
    maxValue = m_histo->interval(Qt::YAxis).maxValue();

    if (scaleDiv.lowerBound() < 0.0)
        scaleDiv.setLowerBound(0.0);

    if (scaleDiv.upperBound() > maxValue)
        scaleDiv.setUpperBound(maxValue);

    m_d->m_plot->setAxisScaleDiv(QwtPlot::yLeft, scaleDiv);
    replot();
#endif
    replot();
}

void Histo2DWidget::updateCursorInfoLabel()
{
    double plotX = m_cursorPosition.x();
    double plotY = m_cursorPosition.y();
    s64 binX = -1;
    s64 binY = -1;
    double value = 0.0;

    if (m_histo)
    {
        binX = m_histo->getAxisBinning(Qt::XAxis).getBin(plotX);
        binY = m_histo->getAxisBinning(Qt::YAxis).getBin(plotY);

        value = m_histo->getValue(plotX, plotY);
    }
    else if (m_histo1DSink && m_histo1DSink->getNumberOfHistos() > 0)
    {
        /* x goes from 0 to #histos.
         * For y the x binning of the first histo is used. */

        auto xBinning = AxisBinning(m_histo1DSink->getNumberOfHistos(), 0.0, m_histo1DSink->getNumberOfHistos());
        binX = xBinning.getBin(plotX);
        binY = m_histo1DSink->getHisto(0)->getAxisBinning(Qt::XAxis).getBin(plotY);

        auto histData = reinterpret_cast<Histo1DListRasterData *>(m_plotItem->data());
        value = histData->value(plotX, plotY);
    }

    if (binX >= 0 && binY >= 0)
    {
        if (std::isnan(value))
            value = 0.0;

        auto text = (QString("x=%1, "
                             "y=%2, "
                             "z=%3\n"
                             "xbin=%4, "
                             "ybin=%5"
                            )
                     .arg(plotX, 0, 'g', 6)
                     .arg(plotY, 0, 'g', 6)
                     .arg(value)
                     .arg(binX)
                     .arg(binY));

        // update the label which will calculate a new width
        m_d->m_labelCursorInfo->setText(text);
    }

    // use the largest width the label ever had to stop the label from constantly changing its width
    if (m_d->m_labelCursorInfo->isVisible())
    {
        m_d->m_labelCursorInfoMaxWidth = std::max(m_d->m_labelCursorInfoMaxWidth, m_d->m_labelCursorInfo->width());
        m_d->m_labelCursorInfo->setMinimumWidth(m_d->m_labelCursorInfoMaxWidth);

        m_d->m_labelCursorInfo->setMinimumHeight(m_d->m_labelCursorInfoMaxHeight);
        m_d->m_labelCursorInfoMaxHeight = std::max(m_d->m_labelCursorInfoMaxHeight, m_d->m_labelCursorInfo->height());
    }
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

void Histo2DWidget::setSink(const SinkPtr &sink, HistoSinkCallback addSinkCallback, HistoSinkCallback sinkModifiedCallback,
                            MakeUniqueOperatorNameFunction makeUniqueOperatorNameFunction)
{
    Q_ASSERT(m_histo && sink && sink->m_histo.get() == m_histo);

    m_sink = sink;
    m_addSinkCallback = addSinkCallback;
    m_sinkModifiedCallback = sinkModifiedCallback;
    m_makeUniqueOperatorNameFunction = makeUniqueOperatorNameFunction;
    m_d->m_actionSubRange->setEnabled(true);
    m_d->m_actionChangeRes->setEnabled(true);
}

void Histo2DWidget::on_tb_subRange_clicked()
{
    Q_ASSERT(m_sink);

    double visibleMinX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    double visibleMinY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).lowerBound();
    double visibleMaxY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).upperBound();

    Histo2DSubRangeDialog dialog(m_sink, m_addSinkCallback, m_sinkModifiedCallback,
                                 m_makeUniqueOperatorNameFunction,
                                 visibleMinX, visibleMaxX, visibleMinY, visibleMaxY,
                                 this);
    dialog.exec();
}

void Histo2DWidget::doXProjection()
{
    double visibleMinX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    double visibleMinY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).lowerBound();
    double visibleMaxY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).upperBound();

    Histo1DPtr histo;

    if (m_histo)
    {
        histo = make_x_projection(m_histo,
                                  visibleMinX, visibleMaxX,
                                  visibleMinY, visibleMaxY);
    }
    else if (m_histo1DSink)
    {
        histo = make_projection(m_histo1DSink->m_histos, Qt::XAxis,
                                visibleMinX, visibleMaxX,
                                visibleMinY, visibleMaxY);
    }

    if (!m_xProjWidget)
    {
        m_xProjWidget = new Histo1DWidget(histo);
        m_xProjWidget->setWindowIcon(QIcon(":/window_icon.png"));
        m_xProjWidget->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_xProjWidget, &QObject::destroyed, this, [this] (QObject *) {
            m_xProjWidget = nullptr;
        });
        add_widget_close_action(m_xProjWidget);

        QString stateKey;

        if (m_histo)
        {
            stateKey = (m_sink ? m_sink->getId().toString() : m_histo->objectName());
        }
        else if (m_histo1DSink)
        {
            stateKey = m_histo1DSink->getId().toString() + QSL("_combined");
        }

        stateKey = stateKey + QSL("_xProj");
        m_geometrySaver->addAndRestore(m_xProjWidget, QSL("WindowGeometries/") + stateKey);
    }
    else
    {
        m_xProjWidget->setHistogram(histo);
    }
}

void Histo2DWidget::doYProjection()
{
    double visibleMinX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    double visibleMinY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).lowerBound();
    double visibleMaxY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).upperBound();

    Histo1DPtr histo;

    if (m_histo)
    {

        histo = make_y_projection(m_histo,
                                  visibleMinX, visibleMaxX,
                                  visibleMinY, visibleMaxY);
    }
    else if (m_histo1DSink)
    {
        histo = make_projection(m_histo1DSink->m_histos, Qt::YAxis,
                                visibleMinX, visibleMaxX,
                                visibleMinY, visibleMaxY);
    }

    if (!m_yProjWidget)
    {
        m_yProjWidget = new Histo1DWidget(histo);
        m_yProjWidget->setWindowIcon(QIcon(":/window_icon.png"));
        m_yProjWidget->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_yProjWidget, &QObject::destroyed, this, [this] (QObject *) {
            m_yProjWidget = nullptr;
        });
        add_widget_close_action(m_yProjWidget);

        QString stateKey;

        if (m_histo)
        {
            stateKey = (m_sink ? m_sink->getId().toString() : m_histo->objectName());
        }
        else if (m_histo1DSink)
        {
            stateKey = m_histo1DSink->getId().toString() + QSL("_combined");
        }

        stateKey = stateKey + QSL("_yProj");
        m_geometrySaver->addAndRestore(m_yProjWidget, QSL("WindowGeometries/") + stateKey);
    }
    else
    {
        m_yProjWidget->setHistogram(histo);
    }
}

void Histo2DWidget::on_tb_projX_clicked()
{
    doXProjection();

    m_xProjWidget->show();
    m_xProjWidget->raise();
}

void Histo2DWidget::on_tb_projY_clicked()
{
    doYProjection();

    m_yProjWidget->show();
    m_yProjWidget->raise();
}

bool Histo2DWidget::event(QEvent *e)
{
    if (e->type() == QEvent::StatusTip)
    {
        m_d->m_statusBar->showMessage(reinterpret_cast<QStatusTipEvent *>(e)->tip());
        return true;
    }

    return QWidget::event(e);
}

QwtPlot *Histo2DWidget::getQwtPlot()
{
    return m_d->m_plot;
}
