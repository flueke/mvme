/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "analysis/analysis_fwd.h"
#include "histo2d_widget_p.h"

#include <qwt_color_map.h>
#include <qwt_picker_machine.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_shapeitem.h>
#include <qwt_plot_spectrogram.h>
#include <qwt_plot_textlabel.h>
#include <qwt_matrix_raster_data.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>

#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFormLayout>
#include <QMenu>
#include <QStack>
#include <QStatusBar>
#include <QStatusTipEvent>
#include <QTimer>
#include <QToolBar>

#include "analysis/a2_adapter.h"
#include "analysis/analysis.h"
#include "git_sha1.h"
#include "histo1d_widget.h"
#include "histo_gui_util.h"
#include "mvme_context_lib.h"
#include "mvme_qwt.h"
#include "qt_util.h"
#include "scrollzoomer.h"
#include "util.h"

static const s32 ReplotPeriod_ms = 1000;

using Intervals = Histo2DStatistics::Intervals;

struct RasterDataBase: public QwtMatrixRasterData
{
    ResolutionReductionFactors m_rrf;

#ifndef QT_NO_DEBUG
    /* Counts the number of samples obtained by qwt when doing a replot. Has to be atomic
     * as QwtPlotSpectrogram::renderImage() uses threaded rendering internally.
     * The number of samples heavily depends on the result of the pixelHint() method and
     * is performance critical. */
    mutable std::atomic<u64> m_sampledValuesForLastReplot;
#endif

    RasterDataBase()
#ifndef QT_NO_DEBUG
        : m_sampledValuesForLastReplot(0u)
#endif
    {}

    void setResolutionReductionFactors(u32 rrfX, u32 rrfY) { m_rrf = { rrfX, rrfY }; }
    void setResolutionReductionFactors(const ResolutionReductionFactors &rrf) { m_rrf = rrf; }

    virtual void initRaster(const QRectF &area, const QSize &raster) override
    {
        preReplot();
        QwtRasterData::initRaster(area, raster);
    }

    virtual void discardRaster() override
    {
        postReplot();
        QwtRasterData::discardRaster();
    }

    void preReplot()
    {
#ifndef QT_NO_DEBUG
        //qDebug() << __PRETTY_FUNCTION__ << this;
        m_sampledValuesForLastReplot = 0u;
#endif
    }

    void postReplot()
    {
#ifndef QT_NO_DEBUG
        //qDebug() << __PRETTY_FUNCTION__ << this
        //    << "sampled values for last replot: " << m_sampledValuesForLastReplot;
#endif
    }
};

struct Histo2DRasterData: public RasterDataBase
{
    Histo2D *m_histo;

    explicit Histo2DRasterData(Histo2D *histo)
        : RasterDataBase()
        , m_histo(histo)
    {
    }

    virtual double value(double x, double y) const override
    {
#ifndef QT_NO_DEBUG
        m_sampledValuesForLastReplot++;
#endif
        //qDebug() << __PRETTY_FUNCTION__ << this
        //    << "x" << x << ", y" << y;

        double v = m_histo->getValue(x, y, m_rrf);
        double r = (v > 0.0 ? v : make_quiet_nan());
        return r;
    }

    virtual QRectF pixelHint(const QRectF &) const override
    {
        QRectF result
        {
            0.0, 0.0,
            m_histo->getAxisBinning(Qt::XAxis).getBinWidth(m_rrf.x),
            m_histo->getAxisBinning(Qt::YAxis).getBinWidth(m_rrf.y)
        };

        //qDebug() << __PRETTY_FUNCTION__ << ">>>>>>" << result;

        return result;
    }

};

using HistoList = QVector<std::shared_ptr<Histo1D>>;

struct Histo1DListRasterData: public RasterDataBase
{
    HistoList m_histos;

    explicit Histo1DListRasterData(const HistoList &histos)
        : RasterDataBase()
        , m_histos(histos)
    {}

    virtual double value(double x, double y) const override
    {
#ifndef QT_NO_DEBUG
        m_sampledValuesForLastReplot++;
#endif
        int histoIndex = x;

        if (histoIndex < 0 || histoIndex >= m_histos.size())
            return make_quiet_nan();

        double v = m_histos[histoIndex]->getValue(y, m_rrf.y);
        double r = (v > 0.0 ? v : make_quiet_nan());
        return r;
    }

    virtual QRectF pixelHint(const QRectF &/*area*/) const override
    {
        double sizeX = 1.0;
        double sizeY = 1.0;

        if (!m_histos.isEmpty())
        {
            sizeY = m_histos[0]->getBinWidth(m_rrf.y);
        }

        QRectF result
        {
            0.0, 0.0, sizeX, sizeY
        };

        //qDebug() << __PRETTY_FUNCTION__ << ">>>>>>" << result;

        return result;
    }
};

using Histo1DSinkPtr = Histo2DWidget::Histo1DSinkPtr;

static Histo2DStatistics calc_Histo1DSink_combined_stats(const Histo1DSinkPtr &sink,
                                                         AxisInterval xInterval,
                                                         AxisInterval yInterval,
                                                         analysis::A2AdapterState *a2State,
                                                         const u32 rrfY)
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

    for (s32 hi = firstHistoIndex; hi <= lastHistoIndex; hi++)
    {
        auto histo  = sink->m_histos.at(hi);
        auto stats = histo->calcStatistics(yInterval.minValue, yInterval.maxValue, rrfY);

        result.entryCount += stats.entryCount;

        if (stats.maxValue > result.maxZ)
        {
            result.maxBinX = hi;
            result.maxBinY = stats.maxBin;
            result.maxX = hi;
            result.maxY = histo->getBinLowEdge(stats.maxBin, rrfY);
            result.maxZ = stats.maxValue;
        }
    }

    auto firstHisto   = sink->m_histos.at(firstHistoIndex);
    auto firstBinning = firstHisto->getAxisBinning(Qt::XAxis);

    result.intervals[Qt::XAxis] = {
        static_cast<double>(firstHistoIndex),
        static_cast<double>(lastHistoIndex + 1)
    };
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
            *m_actionInfo,
            *m_actionCreateCut;

    QComboBox *m_zScaleCombo;

    std::unique_ptr<QwtPlotSpectrogram> m_plotItem;
    ScrollZoomer *m_zoomer;
    QwtText *m_waterMarkText;
    QwtPlotTextLabel *m_waterMarkLabel;

    // Resolution Reduction
    QSlider *m_rrSliderX;
    QSlider *m_rrSliderY;

    QLabel *m_rrLabelX;
    QLabel *m_rrLabelY;

    QWidget *m_rrSliderXContainer;

    ResolutionReductionFactors m_rrf = {};

    // Cuts / Conditions
    analysis::ConditionPtr m_editingCondition;
    QwtPlotPicker *m_cutPolyPicker;
    std::unique_ptr<QwtPlotShapeItem> m_cutShapeItem;

    Histo2D *m_histo = nullptr;
    Histo2DPtr m_histoPtr;
    Histo1DSinkPtr m_histo1DSink;
    QTimer *m_replotTimer;
    QPointF m_cursorPosition;
    int m_labelCursorInfoWidth;

    std::shared_ptr<analysis::Histo2DSink> m_sink;
    Histo2DWidget::HistoSinkCallback m_addSinkCallback;
    Histo2DWidget::HistoSinkCallback m_sinkModifiedCallback;
    Histo2DWidget::MakeUniqueOperatorNameFunction m_makeUniqueOperatorNameFunction;

    Histo1DWidget *m_xProjWidget = nullptr;
    Histo1DWidget *m_yProjWidget = nullptr;

    WidgetGeometrySaver *m_geometrySaver;
    AnalysisServiceProvider *m_serviceProvider = nullptr;

    mvme_qwt::TextLabelItem *m_statsTextItem;
    std::unique_ptr<QwtText> m_statsText;
    mvme_qwt::TextLabelRowLayout m_textLabelLayout;

    void onActionChangeResolution()
    {
        auto combo_xBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);
        auto combo_yBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);

        select_by_resolution(combo_xBins, m_sink->m_xBins);
        select_by_resolution(combo_yBins, m_sink->m_yBins);

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
            auto sink  = m_sink;
            auto xBins = combo_xBins->currentData().toInt();
            auto yBins = combo_yBins->currentData().toInt();

            bool modified = (sink->m_xBins != xBins || sink->m_yBins != yBins);

            if (modified)
            {
                AnalysisPauser pauser(m_serviceProvider);

                sink->m_xBins = xBins;
                sink->m_yBins = yBins;
                sink->setResolutionReductionFactors({});
                m_serviceProvider->analysisOperatorEdited(sink);

                m_rrSliderX->setMaximum(std::log2(xBins));
                m_rrSliderX->setValue(m_rrSliderX->maximum());

                m_rrSliderY->setMaximum(std::log2(yBins));
                m_rrSliderY->setValue(m_rrSliderY->maximum());
            }
        }
    }

    void onRRSliderXValueChanged(int sliderValue);
    void onRRSliderYValueChanged(int sliderValue);

    void onCutPolyPickerActivated(bool on);

    void updatePlotStatsTextBox(const Histo2DStatistics &stats);
};

enum class AxisScaleType
{
    Linear,
    Logarithmic
};

/* The private constructor doing most of the object creation and initialization. To be
 * invoked by all other, more specific constructors. */
Histo2DWidget::Histo2DWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<Histo2DWidgetPrivate>())
{
    m_d->m_q = this;

    m_d->m_plotItem = std::make_unique<QwtPlotSpectrogram>();
    m_d->m_replotTimer = new QTimer(this);
    m_d->m_cursorPosition = { make_quiet_nan(), make_quiet_nan() };
    m_d->m_labelCursorInfoWidth = -1;
    m_d->m_geometrySaver = new WidgetGeometrySaver(this);

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

    // Z-Scale Selection
    {
        m_d->m_zScaleCombo = new QComboBox;
        auto zScaleCombo = m_d->m_zScaleCombo;

        zScaleCombo->addItem(QSL("Lin"), static_cast<int>(AxisScaleType::Linear));
        zScaleCombo->addItem(QSL("Log"), static_cast<int>(AxisScaleType::Logarithmic));

        connect(zScaleCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                this, &Histo2DWidget::displayChanged);

        tb->addWidget(make_vbox_container(QSL("Z-Scale"), zScaleCombo, 2, -2)
                      .container.release());
    }

    tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("X-Proj"),
                  this, &Histo2DWidget::on_tb_projX_clicked);
    tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("Y-Proj"),
                  this, &Histo2DWidget::on_tb_projY_clicked);
    // Connected by other constructors
    m_d->m_actionClear = tb->addAction(QIcon(":/clear_histos.png"), QSL("Clear"));

    // export plot to file / clipboard
    {
        auto menu = new QMenu(this);
        menu->addAction(QSL("to file"), this, &Histo2DWidget::exportPlot);
        menu->addAction(QSL("to clipboard"), this, &Histo2DWidget::exportPlotToClipboard);

        auto button = make_toolbutton(QSL(":/document-pdf.png"), QSL("Export"));
        button->setStatusTip(QSL("Export plot to file or clipboard"));
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);

        tb->addWidget(button);
    }

    m_d->m_actionSubRange = tb->addAction(QIcon(":/histo_subrange.png"), QSL("Subrange"),
                                          this, &Histo2DWidget::on_tb_subRange_clicked);
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

    // Resolution Reduction X
    {
        m_d->m_rrSliderX = make_res_reduction_slider();
        auto boxStruct = make_vbox_container(QSL("Visible X Resolution"), m_d->m_rrSliderX, 0, -2);
        m_d->m_rrLabelX = boxStruct.label;
        m_d->m_rrSliderXContainer = boxStruct.container.get();
        tb->addWidget(boxStruct.container.release());

        connect(m_d->m_rrSliderX, &QSlider::valueChanged, this, [this] (int sliderValue) {
            m_d->onRRSliderXValueChanged(sliderValue);
        });
    }

    // Resolution Reduction Y
    {
        m_d->m_rrSliderY = make_res_reduction_slider();
        auto boxStruct = make_vbox_container(QSL("Visible Y Resolution"), m_d->m_rrSliderY, 0, -2);
        m_d->m_rrLabelY = boxStruct.label;
        tb->addWidget(boxStruct.container.release());

        connect(m_d->m_rrSliderY, &QSlider::valueChanged, this, [this] (int sliderValue) {
            m_d->onRRSliderYValueChanged(sliderValue);
        });
    }

    // XXX: cut test
    {
#if 1
        QPen pickerPen(Qt::red);


        // polygon picker for cut creation
        m_d->m_cutPolyPicker = new QwtPlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                                             QwtPicker::PolygonRubberBand,
                                             QwtPicker::ActiveOnly,
                                             m_d->m_plot->canvas());

        m_d->m_cutPolyPicker->setTrackerPen(pickerPen);
        m_d->m_cutPolyPicker->setRubberBandPen(pickerPen);
        m_d->m_cutPolyPicker->setStateMachine(new QwtPickerPolygonMachine);
        m_d->m_cutPolyPicker->setEnabled(false);

        TRY_ASSERT(connect(m_d->m_cutPolyPicker, &QwtPicker::activated, this, [this](bool on) {
            m_d->onCutPolyPickerActivated(on);
        }));
#endif

#if 1
        auto action = tb->addAction("Dev: Create cut");
        action->setCheckable(true);
        action->setEnabled(false); // will be enabled in setContext()
        m_d->m_actionCreateCut = action;

        connect(action, &QAction::toggled, this, [this](bool checked) {
            if (checked)
            {
                m_d->m_zoomer->setEnabled(false);
                m_d->m_cutPolyPicker->setEnabled(true);
            }
            else
            {
                m_d->m_zoomer->setEnabled(true);
                m_d->m_cutPolyPicker->setEnabled(false);
            }
        });
#endif
    }

    tb->addWidget(make_spacer_widget());

    // Plot

    m_d->m_plotItem->setRenderThreadCount(0); // use system specific ideal thread count
    m_d->m_plotItem->setColorMap(getColorMap());
    m_d->m_plotItem->attach(m_d->m_plot);

    auto rightAxis = m_d->m_plot->axisWidget(QwtPlot::yRight);
    rightAxis->setTitle("Counts");
    rightAxis->setColorBarEnabled(true);
    m_d->m_plot->enableAxis(QwtPlot::yRight);

    connect(m_d->m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_d->m_replotTimer->start(ReplotPeriod_ms);

    m_d->m_plot->canvas()->setMouseTracking(true);

    m_d->m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());
    m_d->m_zoomer->setObjectName("zoomer");

    TRY_ASSERT(connect(m_d->m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));

    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &Histo2DWidget::mouseCursorMovedToPlotCoord));

    TRY_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorLeftPlot,
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

    // stats box (inside the plot)
    m_d->m_statsText = make_qwt_text_box(Qt::AlignTop | Qt::AlignRight);
    m_d->m_statsTextItem = new mvme_qwt::TextLabelItem();
    m_d->m_statsTextItem->setZ(m_d->m_plotItem->z() + 1);

    m_d->m_textLabelLayout.addTextLabel(m_d->m_statsTextItem);
    m_d->m_textLabelLayout.attachAll(m_d->m_plot);

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

Histo2DWidget::Histo2DWidget(const Histo2DPtr histoPtr, QWidget *parent)
    : Histo2DWidget(histoPtr.get(), parent)
{
    m_d->m_histoPtr = histoPtr;
}

Histo2DWidget::Histo2DWidget(Histo2D *histo, QWidget *parent)
    : Histo2DWidget(parent)
{
    m_d->m_histo = histo;
    auto histData = new Histo2DRasterData(m_d->m_histo);
    m_d->m_plotItem->setData(histData);

    connect(m_d->m_histo, &Histo2D::axisBinningChanged, this, [this] (Qt::Axis) {
        // Handle axis changes by zooming out fully. This will make sure
        // possible axis scale changes are immediately visible and the zoomer
        // is in a clean state.
        m_d->m_zoomer->setZoomStack(QStack<QRectF>(), -1);
        m_d->m_zoomer->zoom(0);
        replot();
    });

    connect(m_d->m_actionClear, &QAction::triggered, this, [this]() {
        m_d->m_histo->clear();
        replot();
    });

    m_d->m_rrSliderX->setMaximum(std::log2(m_d->m_histo->getAxisBinning(Qt::XAxis).getBinCount()));
    m_d->m_rrSliderX->setValue(m_d->m_rrSliderX->maximum());

    m_d->m_rrSliderY->setMaximum(std::log2(m_d->m_histo->getAxisBinning(Qt::YAxis).getBinCount()));
    m_d->m_rrSliderY->setValue(m_d->m_rrSliderY->maximum());

    displayChanged();
}

Histo2DWidget::Histo2DWidget(const Histo1DSinkPtr &histo1DSink, AnalysisServiceProvider *serviceProvider, QWidget *parent)
    : Histo2DWidget(parent)
{
    Q_ASSERT(histo1DSink);
    Q_ASSERT(serviceProvider);

    m_d->m_serviceProvider = serviceProvider;
    m_d->m_histo1DSink = histo1DSink;
    auto histData = new Histo1DListRasterData(m_d->m_histo1DSink->m_histos);
    m_d->m_plotItem->setData(histData);

    m_d->m_rrSliderX->setMaximum(histo1DSink->getNumberOfHistos());
    m_d->m_rrSliderX->setValue(m_d->m_rrSliderX->maximum());
    m_d->m_rrSliderX->setEnabled(false);
    m_d->m_rrSliderXContainer->setVisible(false);
    for (auto childWidget: m_d->m_rrSliderXContainer->findChildren<QWidget *>())
    {
        childWidget->setVisible(false);
    }

    if (m_d->m_histo1DSink->getNumberOfHistos())
    {
        auto histo = m_d->m_histo1DSink->getHisto(0);
        m_d->m_rrSliderY->setMaximum(std::log2(histo->getNumberOfBins()));
        m_d->m_rrSliderY->setValue(m_d->m_rrSliderY->maximum());
    }
    else
    {
        m_d->m_rrSliderY->setMinimum(0);
        m_d->m_rrSliderY->setMaximum(0);
    }

    connect(m_d->m_actionClear, &QAction::triggered, this, [this]() {
        for (auto &histo: m_d->m_histo1DSink->m_histos)
        {
            histo->clear();
        }
        replot();
    });

    displayChanged();
}


Histo2DWidget::~Histo2DWidget()
{
}

void Histo2DWidget::setServiceProvider(AnalysisServiceProvider *asp)
{
    m_d->m_serviceProvider = asp;
#if 1
    m_d->m_actionCreateCut->setEnabled(asp != nullptr);
#endif
}

void Histo2DWidget::replot()
{
    /* Things that have to happen:
     * - calculate stats for the visible area. use this to scale z
     * - update info display
     * - update stats text box
     * - update cursor info
     * - update axis titles
     * - update window title
     * - update projections
     */

    const auto rrf = m_d->m_rrf;

    //qDebug() << __PRETTY_FUNCTION__ << "rrf =" << rrf;

    QwtInterval visibleXInterval = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).interval();
    QwtInterval visibleYInterval = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).interval();

    // If fully zoomed out set axis scales to full size and use that as the zoomer base.
    if (m_d->m_zoomer->zoomRectIndex() == 0)
    {
        if (m_d->m_histo)
        {
            visibleXInterval =
            {
                m_d->m_histo->getAxisBinning(Qt::XAxis).getMin(),
                m_d->m_histo->getAxisBinning(Qt::XAxis).getMax()
            };

            visibleYInterval =
            {
                m_d->m_histo->getAxisBinning(Qt::YAxis).getMin(),
                m_d->m_histo->getAxisBinning(Qt::YAxis).getMax()
            };
        }
        else if (m_d->m_histo1DSink)
        {
            auto firstHisto   = m_d->m_histo1DSink->m_histos.at(0);
            auto firstBinning = firstHisto->getAxisBinning(Qt::XAxis);

            visibleXInterval =
            {
                0.0,
                static_cast<double>(m_d->m_histo1DSink->m_histos.size())
            };

            visibleYInterval =
            {
                firstBinning.getMin(),
                firstBinning.getMax(),
            };
        }

        m_d->m_plot->setAxisScale(QwtPlot::xBottom,
                                  visibleXInterval.minValue(),
                                  visibleXInterval.maxValue());

        m_d->m_plot->setAxisScale(QwtPlot::yLeft,
                                  visibleYInterval.minValue(),
                                  visibleYInterval.maxValue());

        m_d->m_zoomer->setZoomBase();
    }

    Histo2DStatistics stats;

    if (m_d->m_histo)
    {
        stats = m_d->m_histo->calcStatistics(
            { visibleXInterval.minValue(), visibleXInterval.maxValue() },
            { visibleYInterval.minValue(), visibleYInterval.maxValue() },
            rrf);
    }
    else if (m_d->m_histo1DSink)
    {
        stats = calc_Histo1DSink_combined_stats(
            m_d->m_histo1DSink,
            { visibleXInterval.minValue(), visibleXInterval.maxValue() },
            { visibleYInterval.minValue(), visibleYInterval.maxValue() },
            m_d->m_serviceProvider->getAnalysis()->getA2AdapterState(),
            rrf.y);
    }


    // Since qwt-6.2.0 having a zero-width z-scale leads to visual corruption
    // when replotting. This is not an issue with the histogram implementation
    // as qwt in this case does not even request values from the histogram.
    // The code below ensures that the z-axis always has a non-zero width to
    // work around this issue.

    // Convert the histo stats intervals to QwtIntervals.
    std::array<QwtInterval, 3> intervals;
    for (size_t axis = 0; axis < stats.intervals.size(); ++axis)
        intervals[axis] = { stats.intervals[axis].minValue, stats.intervals[axis].maxValue };

    auto &zInterval = intervals[Qt::ZAxis];

    //qDebug("%s stats zInterval: %lf, %lf, valid=%d, width=%lf",
    //       __PRETTY_FUNCTION__, zInterval.minValue(), zInterval.maxValue(), zInterval.isValid(), zInterval.width());

    // Z axis handling for log scales: start from 1.0
    double zBase = zAxisIsLog() ? 1.0 : 0.0;
    zInterval.setMinValue(zBase);

    // Make sure it's non-zero width
    if (zInterval.width() <= 0.0)
        zInterval.setMaxValue(zAxisIsLog() ? 2.0 : 1.0);

    assert(zInterval.width() > 0.0);

    //qDebug("%s adjusted zInterval: %lf, %lf, valid=%d, width=%lf",
    //       __PRETTY_FUNCTION__, zInterval.minValue(), zInterval.maxValue(), zInterval.isValid(), zInterval.width());

    // Set the intervals on the interal raster data object.
    auto rasterData = reinterpret_cast<RasterDataBase *>(m_d->m_plotItem->data());
    for (size_t axis = 0; axis < intervals.size(); ++axis)
        rasterData->setInterval(static_cast<Qt::Axis>(axis), intervals[axis]);

    // Important: Setting the rrf has to happen before updateCursorInfoLabel()
    // as that calls Histo1DListRasterData::value() internally  which uses the
    // rrf.
    rasterData->setResolutionReductionFactors(rrf);

    m_d->m_plot->setAxisScale(QwtPlot::yRight, zInterval.minValue(), zInterval.maxValue());

    auto axis = m_d->m_plot->axisWidget(QwtPlot::yRight);
    axis->setColorMap(zInterval, getColorMap());

    // cursor info
    updateCursorInfoLabel();

    // window and axis titles
    if (m_d->m_histo)
    {
        QStringList pathParts;

        if (m_d->m_sink)
            pathParts = analysis::make_parent_path_list(m_d->m_sink);

        pathParts.push_back(m_d->m_histo->objectName());
        setWindowTitle(pathParts.join('/'));

        auto axisInfo = m_d->m_histo->getAxisInfo(Qt::XAxis);
        m_d->m_plot->axisWidget(QwtPlot::xBottom)->setTitle(make_title_string(axisInfo));

        axisInfo = m_d->m_histo->getAxisInfo(Qt::YAxis);
        m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle(make_title_string(axisInfo));
    }
    else if (m_d->m_histo1DSink)
    {
        auto pathParts = analysis::make_parent_path_list(m_d->m_histo1DSink);
        pathParts.push_back(QString("%1 2D combined").arg(m_d->m_histo1DSink->objectName()));
        setWindowTitle(pathParts.join('/'));

        m_d->m_plot->axisWidget(QwtPlot::xBottom)->setTitle(QSL("Histogram #"));

        // Use the first histograms x axis as the title for the combined y axis
        if (auto histo = m_d->m_histo1DSink->getHisto(0))
        {
            auto axisInfo = histo->getAxisInfo(Qt::XAxis);
            m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle(make_title_string(axisInfo));
        }
    }
    else
    {
        InvalidCodePath;
    }

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

    m_d->updatePlotStatsTextBox(stats);

    // tell qwt to replot
    m_d->m_plot->replot();

    // projections
    if (m_d->m_xProjWidget)
    {
        doXProjection();
    }

    if (m_d->m_yProjWidget)
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

    m_d->m_plotItem->setColorMap(getColorMap());

    replot();
}

void Histo2DWidget::exportPlot()
{
    QString fileName;
    QString title;
    QString footer;

    if (m_d->m_histo)
    {
        fileName = m_d->m_histo->objectName();
        title    = m_d->m_histo->getTitle();
        footer   = m_d->m_histo->getFooter();
    }
    else if (m_d->m_histo1DSink)
    {
        fileName = m_d->m_histo1DSink->objectName();
        title = windowTitle();
        // just use the first histograms footer
        if (auto h1d = m_d->m_histo1DSink->m_histos.value(0))
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

    if (m_d->m_serviceProvider)
    {
        fileName = QDir(m_d->m_serviceProvider->getWorkspacePath(QSL("PlotsDirectory"))).filePath(fileName);
    }

    m_d->m_plot->setTitle(title);


    QwtText footerText(footer);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_d->m_plot->setFooter(footerText);
    m_d->m_waterMarkLabel->show();

    bool infoWasVisible = m_d->m_actionInfo->isChecked();
    m_d->m_actionInfo->setChecked(true);

    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.exportTo(m_d->m_plot, fileName);

    m_d->m_actionInfo->setChecked(infoWasVisible);
    m_d->m_plot->setTitle(QString());
    m_d->m_plot->setFooter(QString());
    m_d->m_waterMarkLabel->hide();
}

void Histo2DWidget::exportPlotToClipboard()
{
    QString title;
    QString footer;

    if (m_d->m_histo)
    {
        title  = m_d->m_histo->getTitle();
        footer = m_d->m_histo->getFooter();
    }
    else if (m_d->m_histo1DSink)
    {
        title = windowTitle();
        // just use the first histograms footer
        if (auto h1d = m_d->m_histo1DSink->m_histos.value(0))
        {
            footer = h1d->getFooter();
        }
    }
    else
    {
        InvalidCodePath;
    }

    m_d->m_plot->setTitle(title);


    QwtText footerText(footer);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_d->m_plot->setFooter(footerText);
    m_d->m_waterMarkLabel->show();

    QSize size(1024, 768);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QwtPlotRenderer renderer;
#ifndef Q_OS_WIN
    // Enabling this leads to black pixels when pasting the image into windows
    // paint.
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground
                             | QwtPlotRenderer::DiscardCanvasBackground);
#endif
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.renderTo(m_d->m_plot, image);

    m_d->m_plot->setTitle(QString());
    m_d->m_plot->setFooter(QString());
    m_d->m_waterMarkLabel->hide();

    auto clipboard = QApplication::clipboard();
    clipboard->clear();
    clipboard->setImage(image);
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
    m_d->m_cursorPosition = pos;
    updateCursorInfoLabel();
}

void Histo2DWidget::mouseCursorLeftPlot()
{
    m_d->m_cursorPosition = QPointF(make_quiet_nan(), make_quiet_nan());
    updateCursorInfoLabel();
}

void Histo2DWidget::zoomerZoomed(const QRectF &)
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

// TODO: RR
void Histo2DWidget::updateCursorInfoLabel()
{
    double plotX = m_d->m_cursorPosition.x();
    double plotY = m_d->m_cursorPosition.y();
    s64 binX = -1;
    s64 binY = -1;
    double value = 0.0;

    if (m_d->m_histo)
    {
        binX = m_d->m_histo->getAxisBinning(Qt::XAxis).getBin(plotX, m_d->m_rrf.x);
        binY = m_d->m_histo->getAxisBinning(Qt::YAxis).getBin(plotY, m_d->m_rrf.y);

        value = m_d->m_histo->getValue(plotX, plotY, m_d->m_rrf);
    }
    else if (m_d->m_histo1DSink && m_d->m_histo1DSink->getNumberOfHistos() > 0)
    {
        /* x goes from 0 to #histos.
         * For y the x binning of the first histo is used. */

        auto xBinning = AxisBinning(m_d->m_histo1DSink->getNumberOfHistos(),
                                    0.0, m_d->m_histo1DSink->getNumberOfHistos());
        binX = xBinning.getBin(plotX);
        binY = m_d->m_histo1DSink->getHisto(0)->getAxisBinning(Qt::XAxis)
            .getBin(plotY, m_d->m_rrf.y);

        auto histData = reinterpret_cast<Histo1DListRasterData *>(m_d->m_plotItem->data());
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
        m_d->m_labelCursorInfoMaxWidth = std::max(m_d->m_labelCursorInfoMaxWidth,
                                                  m_d->m_labelCursorInfo->width());
        m_d->m_labelCursorInfo->setMinimumWidth(m_d->m_labelCursorInfoMaxWidth);

        m_d->m_labelCursorInfo->setMinimumHeight(m_d->m_labelCursorInfoMaxHeight);
        m_d->m_labelCursorInfoMaxHeight = std::max(m_d->m_labelCursorInfoMaxHeight,
                                                   m_d->m_labelCursorInfo->height());
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

void Histo2DWidget::setSink(const SinkPtr &sink,
                            HistoSinkCallback addSinkCallback,
                            HistoSinkCallback sinkModifiedCallback,
                            MakeUniqueOperatorNameFunction makeUniqueOperatorNameFunction)
{
    Q_ASSERT(m_d->m_histo && sink && sink->m_histo.get() == m_d->m_histo);

    m_d->m_sink = sink;
    m_d->m_addSinkCallback = addSinkCallback;
    m_d->m_sinkModifiedCallback = sinkModifiedCallback;
    m_d->m_makeUniqueOperatorNameFunction = makeUniqueOperatorNameFunction;
    m_d->m_actionSubRange->setEnabled(true);
    m_d->m_actionChangeRes->setEnabled(true);

    auto rrf = sink->getResolutionReductionFactors();

    if (rrf.x == AxisBinning::NoResolutionReduction)
    {
        m_d->m_rrSliderX->setValue(m_d->m_rrSliderX->maximum());
    }
    else
    {
        u32 visBins = m_d->m_histo->getAxisBinning(Qt::XAxis).getBinCount(rrf.x);
        int sliderValue = std::log2(visBins);
        m_d->m_rrSliderX->setValue(sliderValue);
    }

    if (rrf.y == AxisBinning::NoResolutionReduction)
    {
        m_d->m_rrSliderY->setValue(m_d->m_rrSliderY->maximum());
    }
    else
    {
        u32 visBins = m_d->m_histo->getAxisBinning(Qt::YAxis).getBinCount(rrf.x);
        int sliderValue = std::log2(visBins);
        m_d->m_rrSliderY->setValue(sliderValue);
    }
}

void Histo2DWidget::on_tb_subRange_clicked()
{
    Q_ASSERT(m_d->m_sink);

    double visibleMinX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    double visibleMinY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).lowerBound();
    double visibleMaxY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).upperBound();

    Histo2DSubRangeDialog dialog(m_d->m_sink, m_d->m_addSinkCallback, m_d->m_sinkModifiedCallback,
                                 m_d->m_makeUniqueOperatorNameFunction,
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

    if (m_d->m_histo)
    {
        histo = make_x_projection(m_d->m_histo,
                                  visibleMinX, visibleMaxX,
                                  visibleMinY, visibleMaxY);
    }
    else if (m_d->m_histo1DSink)
    {
        histo = make_projection(m_d->m_histo1DSink->m_histos, Qt::XAxis,
                                visibleMinX, visibleMaxX,
                                visibleMinY, visibleMaxY);
    }

    QString projHistoObjectName;

    if (m_d->m_histo)
        projHistoObjectName = m_d->m_histo->objectName() + " X-Projection";
    else if (m_d->m_histo1DSink)
        projHistoObjectName = m_d->m_histo1DSink->objectName() + " Combined X-Projection";

    histo->setObjectName(projHistoObjectName);

    if (!m_d->m_xProjWidget)
    {
        m_d->m_xProjWidget = new Histo1DWidget(histo);
        m_d->m_xProjWidget->setServiceProvider(m_d->m_serviceProvider);
        m_d->m_xProjWidget->setResolutionReductionFactor(m_d->m_rrf.x);
        m_d->m_xProjWidget->setResolutionReductionSliderEnabled(false);
        m_d->m_xProjWidget->setWindowIcon(QIcon(":/window_icon.png"));
        m_d->m_xProjWidget->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_d->m_xProjWidget, &QObject::destroyed, this, [this] (QObject *) {
            m_d->m_xProjWidget = nullptr;
        });
        add_widget_close_action(m_d->m_xProjWidget);

        QString stateKey;

        if (m_d->m_histo)
        {
            stateKey = (m_d->m_sink ? m_d->m_sink->getId().toString() : m_d->m_histo->objectName());
        }
        else if (m_d->m_histo1DSink)
        {
            stateKey = m_d->m_histo1DSink->getId().toString() + QSL("_combined");
        }

        stateKey = stateKey + QSL("_xProj");
        m_d->m_geometrySaver->addAndRestore(m_d->m_xProjWidget, QSL("WindowGeometries/") + stateKey);
    }
    else
    {
        m_d->m_xProjWidget->setHistogram(histo);
        m_d->m_xProjWidget->setResolutionReductionFactor(m_d->m_rrf.x);
    }
}

void Histo2DWidget::doYProjection()
{
    double visibleMinX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    double visibleMinY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).lowerBound();
    double visibleMaxY = m_d->m_plot->axisScaleDiv(QwtPlot::yLeft).upperBound();

    Histo1DPtr histo;

    if (m_d->m_histo)
    {

        histo = make_y_projection(m_d->m_histo,
                                  visibleMinX, visibleMaxX,
                                  visibleMinY, visibleMaxY);
    }
    else if (m_d->m_histo1DSink)
    {
        histo = make_projection(m_d->m_histo1DSink->m_histos, Qt::YAxis,
                                visibleMinX, visibleMaxX,
                                visibleMinY, visibleMaxY);
    }

    QString projHistoObjectName;

    if (m_d->m_histo)
        projHistoObjectName = m_d->m_histo->objectName() + " Y-Projection";
    else if (m_d->m_histo1DSink)
        projHistoObjectName = m_d->m_histo1DSink->objectName() + " Combined Y-Projection";

    histo->setObjectName(projHistoObjectName);

    if (!m_d->m_yProjWidget)
    {
        m_d->m_yProjWidget = new Histo1DWidget(histo);
        m_d->m_yProjWidget->setResolutionReductionFactor(m_d->m_rrf.y);
        m_d->m_yProjWidget->setResolutionReductionSliderEnabled(false);
        m_d->m_yProjWidget->setServiceProvider(m_d->m_serviceProvider);
        m_d->m_yProjWidget->setWindowIcon(QIcon(":/window_icon.png"));
        m_d->m_yProjWidget->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_d->m_yProjWidget, &QObject::destroyed, this, [this] (QObject *) {
            m_d->m_yProjWidget = nullptr;
        });
        add_widget_close_action(m_d->m_yProjWidget);

        QString stateKey;

        if (m_d->m_histo)
        {
            stateKey = (m_d->m_sink ? m_d->m_sink->getId().toString() : m_d->m_histo->objectName());
        }
        else if (m_d->m_histo1DSink)
        {
            stateKey = m_d->m_histo1DSink->getId().toString() + QSL("_combined");
        }

        stateKey = stateKey + QSL("_yProj");
        m_d->m_geometrySaver->addAndRestore(m_d->m_yProjWidget, QSL("WindowGeometries/") + stateKey);
    }
    else
    {
        m_d->m_yProjWidget->setHistogram(histo);
        m_d->m_yProjWidget->setResolutionReductionFactor(m_d->m_rrf.y);
    }
}

void Histo2DWidget::on_tb_projX_clicked()
{
    doXProjection();

    m_d->m_xProjWidget->show();
    m_d->m_xProjWidget->raise();
}

void Histo2DWidget::on_tb_projY_clicked()
{
    doYProjection();

    m_d->m_yProjWidget->show();
    m_d->m_yProjWidget->raise();
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

void Histo2DWidgetPrivate::onRRSliderXValueChanged(int sliderValue)
{
    if (m_histo)
    {
        u32 physBins = m_histo->getAxisBinning(Qt::XAxis).getBinCount();
        u32 visBins  = 1u << sliderValue;
        m_rrf.x = physBins / visBins;

        m_rrLabelX->setText(QSL("Visible X Resolution: %1, %2 bit")
                            .arg(visBins)
                            .arg(std::log2(visBins))
                           );

        //qDebug() << __PRETTY_FUNCTION__
        //    << "rrx adjust: sliderValue =" << sliderValue << "new rrf =" << m_rrf;

        if (m_sink)
        {
            m_sink->setResolutionReductionFactors(m_rrf);
        }
    }
    else if (m_histo1DSink)
    {
        // NOOP for now
    }

    m_q->replot();
}

void Histo2DWidgetPrivate::onRRSliderYValueChanged(int sliderValue)
{
    if (m_histo)
    {
        u32 physBins = m_histo->getAxisBinning(Qt::YAxis).getBinCount();
        u32 visBins  = 1u << sliderValue;
        m_rrf.y = physBins / visBins;

        m_rrLabelY->setText(QSL("Visible Y Resolution: %1, %2 bit")
                            .arg(visBins)
                            .arg(std::log2(visBins))
                           );

        //qDebug() << __PRETTY_FUNCTION__
        //    << "rry adjust: sliderValue =" << sliderValue << "new rrf =" << m_rrf;

        if (m_sink)
        {
            m_sink->setResolutionReductionFactors(m_rrf);
        }

    }
    else if (m_histo1DSink && m_histo1DSink->getNumberOfHistos() > 0)
    {
        auto histo = m_histo1DSink->getHisto(0);

        u32 physBins = histo->getAxisBinning(Qt::XAxis).getBinCount();
        u32 visBins  = 1u << sliderValue;
        m_rrf.y = physBins / visBins;

        m_rrLabelY->setText(QSL("Visible Y Resolution: %1, %2 bit")
                            .arg(visBins)
                            .arg(std::log2(visBins))
                           );

        //qDebug() << __PRETTY_FUNCTION__
        //    << "rry adjust: sliderValue =" << sliderValue << "new rrf =" << m_rrf;
    }

    m_q->replot();
}

void Histo2DWidgetPrivate::onCutPolyPickerActivated(bool active)
{
    assert(m_serviceProvider);
    assert(m_sink);

    // We're only interested in the deactivate, i.e. completion event
    if (active) return;

    auto pixelPoly = m_cutPolyPicker->selection();

    QPolygonF poly;
    poly.reserve(pixelPoly.size() + 1);

    for (const auto &point: pixelPoly)
    {
        poly.push_back(QPointF(
                m_plot->invTransform(QwtPlot::xBottom, point.x()),
                m_plot->invTransform(QwtPlot::yLeft, point.y())
                ));
    }

    // close the poly
    if (!poly.isEmpty())
    {
        poly.push_back(poly.first());
    }

    if (!m_cutShapeItem)
    {
        // create the shape item for rendering the polygon in the plot
        m_cutShapeItem = std::make_unique<QwtPlotShapeItem>(QSL("Cut"));
        m_cutShapeItem->attach(m_plot);

        //QBrush brush(QColor("#d0d78e"), Qt::DiagCrossPattern);
        QBrush brush(Qt::magenta, Qt::DiagCrossPattern);
        m_cutShapeItem->setBrush(brush);
    }

    assert(m_cutShapeItem);

    // render the polygon
    m_cutShapeItem->setPolygon(poly);

    // Back to default ui interactions: disable cut picker, enable zoomer
    m_cutPolyPicker->setEnabled(false);

    // Tell the zoomer to ignore the release following from the last right-click that
    // closed the polygon. This seemed the easiest way to avoid unexpectedly zooming
    // out.
    m_zoomer->ignoreNextMouseRelease();
    m_zoomer->setEnabled(true);

#if 0
    m_actionCreateCut->setChecked(false);
#endif

    m_q->replot();

    // Show a dialog to the user asking for a name for the cut and offering the
    // possibility to cancel cut creation.
    QString cutName = QSL("NewPolyCut");

    {
        auto le_cutName = new QLineEdit;
        le_cutName->setText(cutName);

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

        QDialog dialog(m_q);
        auto layout = new QFormLayout(&dialog);
        layout->addRow("Cut Name", le_cutName);
        layout->addRow(buttons);

        QObject::connect(buttons, &QDialogButtonBox::accepted,
                         &dialog, &QDialog::accept);

        QObject::connect(buttons, &QDialogButtonBox::rejected,
                         &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Rejected)
        {
            m_cutShapeItem->setVisible(false);
            m_q->replot();
            return;
        }

        cutName = le_cutName->text();
    }


    // create a new cut object and add it to the analysis

    auto cond = std::make_shared<analysis::PolygonCondition>();
    cond->setPolygon(poly);
    cond->setObjectName(cutName);

    {
        auto xInput = m_sink->getSlot(0)->inputPipe;
        auto xIndex = m_sink->getSlot(0)->paramIndex;
        auto yInput = m_sink->getSlot(1)->inputPipe;
        auto yIndex = m_sink->getSlot(1)->paramIndex;

        AnalysisPauser pauser(m_serviceProvider);
        cond->connectInputSlot(0, xInput, xIndex);
        cond->connectInputSlot(1, yInput, yIndex);

        const int userLevel = 1;

        m_serviceProvider->getAnalysis()->addOperator(
            m_sink->getEventId(),
            userLevel,
            cond);
    }
}

bool Histo2DWidget::setEditCondition(const analysis::ConditionPtr &cond)
{
    qDebug() << __PRETTY_FUNCTION__ << this << cond.get();

    auto condPoly = qobject_cast<analysis::PolygonCondition *>(cond.get());

    if (!condPoly)
    {
        // clear to avoid returning the previous condition in getEditCondition()
        m_d->m_editingCondition = {};
        return false;
    }

    m_d->m_editingCondition = cond;

    if (!m_d->m_cutShapeItem)
    {
        // create the shape item for rendering the polygon in the plot
        m_d->m_cutShapeItem = std::make_unique<QwtPlotShapeItem>(QSL("Cut"));
        m_d->m_cutShapeItem->attach(m_d->m_plot);

        //QBrush brush(QColor("#d0d78e"), Qt::DiagCrossPattern);
        QBrush brush(Qt::magenta, Qt::DiagCrossPattern);
        m_d->m_cutShapeItem->setBrush(brush);
    }

    assert(m_d->m_cutShapeItem);

    // render the polygon
    m_d->m_cutShapeItem->setPolygon(condPoly->getPolygon());

    return true;
}

analysis::ConditionPtr Histo2DWidget::getEditCondition() const
{
    return m_d->m_editingCondition;
}

void Histo2DWidget::beginEditCondition()
{
    qDebug() << __PRETTY_FUNCTION__ << this << "not implemented!";
}

void Histo2DWidgetPrivate::updatePlotStatsTextBox(const Histo2DStatistics &stats)
{
    static const QString statsTextTemplate = QSL(
        "<table>"
        "<tr><td align=\"left\">Counts </td><td>%L1</td></tr>"
        "<tr><td align=\"left\">Max Z  </td><td>%L2 @ (%L3, %L4)</td></tr>"
        "</table>"
        );

    QString buffer = statsTextTemplate
        .arg(stats.entryCount, 0, 'f', 0)
        .arg(stats.maxZ)
        .arg(stats.maxX, 0, 'g', 6)
        .arg(stats.maxY, 0, 'g', 6)
        ;

    m_statsText->setText(buffer, QwtText::RichText);
    m_statsTextItem->setText(*m_statsText);
}
