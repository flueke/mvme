/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "histo1d_widget.h"
#include "histo1d_widget_p.h"

#include <qwt_interval.h>
#include <qwt_painter.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_plot_zoneitem.h>
#include <qwt_point_data.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_text.h>

#include <qwt_widget_overlay.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStack>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QPrinter>
#include <QPrintDialog>

#include "analysis/analysis_fwd.h"
#include "analysis/analysis_graphs.h"
#include "analysis/analysis.h"
#include "analysis/condition_ui.h"
#include "histo1d_util.h"
#include "histo_gui_util.h"
#include "histo_stats_widget.h"
#include "histo_ui.h"
#include "mvme_context_lib.h"
#include "mvme_qwt.h"
#include "scrollzoomer.h"
#include "util.h"
#include "util/qt_monospace_textedit.h"

#include "git_sha1.h"

using namespace histo_ui;
using namespace mvme_qwt;

// TODO Mon Nov 26 2018
// 1) Combine sink, calibration, context, etc. Having those set on the widget
//    only makes sense in combination. If we have a sink but no context, we can't
//    get to the analysis and thus cannot react to changes.
//
// 2) Find a better way to split up the widget. Isolate core parts that do not
//    need anything from the analysis and only do basic qwt rendering, axis
//    scaling, etc.
// 3) Build a layer using the analysis related parts.

/*
Rate Estimation

Picker with action on point selection

data:
  isvisible, x1, x2
  markers for x1, x2
  plot curve with corresponding curve data (QwtPlotCurve and own subclass of QwtSyntheticPointData)

logic:
  swap x positions to have x1 < x2
  decision on number of points picked (nan is used as a special value)


Gauss
  toggle on/off, no picker involved
  plot curve and data
  The curve data needs access to the most recently calculated histo stats
  */

static const s32 ReplotPeriod_ms = 1000;
static const u32 RRFMin = 2;




struct CalibUi
{
    QDoubleSpinBox *actual1, *actual2,
                   *target1, *target2,
                   *lastFocusedActual;
    QPushButton *applyButton,
                *fillMaxButton,
                *resetToFilterButton,
                *closeButton;

    QDialog *window;
};

struct RateEstimationData
{
    bool visible = false;
    double x1 = make_quiet_nan();
    double x2 = make_quiet_nan();
};

static const double E1 = std::exp(1.0);

class RateEstimationCurveData: public QwtSyntheticPointData
{
    static const size_t NumberOfPoints = 500;

    public:
        RateEstimationCurveData(Histo1D *histo, RateEstimationData *data)
            : QwtSyntheticPointData(NumberOfPoints)
            , m_histo(histo)
            , m_data(data)
        {}

        virtual double y(double x) const override
        {
            auto xy1 = m_histo->getValueAndBinLowEdge(m_data->x1, m_rrf);
            auto xy2 = m_histo->getValueAndBinLowEdge(m_data->x2, m_rrf);

            double x1 = xy1.first;
            double y1 = xy1.second;
            double x2 = xy2.first;
            double y2 = xy2.second;

            double tau = (x2 - x1) / log(y1 / y2);

#if 0
            double norm = y1 / (( 1.0 / tau) * pow(E1, -x1 / tau));

            qDebug() << __PRETTY_FUNCTION__
                << "graphical norm =" << norm
                << "bin / units adjusted norm"
                << norm * m_histo->getAxisBinning(Qt::XAxis).getBinsToUnitsRatio()
                ;
#endif

            double result = y1 * (pow(E1, -x/tau) / pow(E1, -x1/tau));

            return result;
        }

        void setResolutionReductionFactor(u32 rrf) { m_rrf = rrf; }
        u32 getResolutionReductionFactor() const { return m_rrf; }

    private:
        Histo1D *m_histo;
        RateEstimationData *m_data;
        u32 m_rrf = Histo1D::NoRR;
};

static const double PlotTextLayerZ  = 1000.0;
static const double PlotAdditionalCurvesLayerZ = 1010.0;

struct Histo1DWidgetPrivate
{
    Histo1DWidget *m_q;

    QToolBar *m_toolBar;
    QwtPlot *m_plot;
    QStatusBar *m_statusBar;

    QLabel *m_labelCursorInfo;
    QLabel *m_labelHistoInfo;
    QWidget *m_infoContainer;

    s32 m_labelCursorInfoMaxWidth  = 0;
    s32 m_labelCursorInfoMaxHeight = 0;

    QAction *m_actionZoom,
            *m_actionGaussFit,
            *m_actionRateEstimation,
            *m_actionSubRange,
            *m_actionChangeRes,
            *m_actionCalibUi,
            *m_actionInfo,
            *m_actionHistoListStats,
            *m_actionCuts,
            *m_actionConditions;

    QActionGroup *m_exclusiveActions;

    // rate estimation
    RateEstimationData m_rateEstimationData;
    QwtPlotPicker *m_rateEstimationPointPicker;
    QwtPlotMarker *m_rateEstimationX1Marker;
    QwtPlotMarker *m_rateEstimationX2Marker;
    QwtPlotMarker *m_rateEstimationFormulaMarker;
    RateEstimationCurveData *m_plotRateEstimationData = nullptr; // owned by qwt

    // text items / stats
    mvme_qwt::TextLabelItem *m_globalStatsTextItem;
    mvme_qwt::TextLabelItem *m_gaussStatsTextItem;
    mvme_qwt::TextLabelRowLayout m_textLabelLayout;
    std::unique_ptr<QwtText> m_globalStatsText;
    std::unique_ptr<QwtText> m_gaussStatsText;

    Histo1DIntervalData *m_plotHistoData = nullptr; // owned by qwt
    QwtPlotCurve *m_gaussCurve = nullptr;
    QwtPlotCurve *m_rateEstimationCurve = nullptr;

    QwtText m_waterMarkText;
    QwtPlotTextLabel *m_waterMarkLabel;

    QComboBox *m_yScaleCombo;
    QSlider *m_rrSlider;
    QLabel *m_rrLabel;
    u32 m_rrf = Histo1D::NoRR;

    CalibUi m_calibUi;

    // Histo and stats
    Histo1DStatistics m_stats;

    Histo1DWidget::HistoList m_histos;
    s32 m_histoIndex = 0;

    s32 currentHistoIndex() const;

    QSpinBox *m_histoSpin;

    QwtPlotHistogram *m_plotHisto;

    ScrollZoomer *m_zoomer;
    QTimer *m_replotTimer;
    QPointF m_cursorPosition;

    std::shared_ptr<analysis::CalibrationMinMax> m_calib;
    AnalysisServiceProvider *m_serviceProvider = nullptr;

    Histo1DWidget::SinkPtr m_sink;
    Histo1DWidget::HistoSinkCallback m_sinkModifiedCallback;

    analysis::ui::IntervalConditionEditorController *m_intervalConditionEditorController = nullptr;

    int histoStyle_ = QwtPlotHistogram::Outline;
    HistoStatsWidget *histoStatsWidget_ = nullptr;

    void setCalibUiVisible(bool b)
    {
        auto window = m_calibUi.window;
        window->setVisible(b);

        s32 x = m_q->width() - window->width();
        s32 y = m_toolBar->height() + 15;
        m_calibUi.window->move(m_q->mapToGlobal(QPoint(x, y)));
    }

    void onActionChangeResolution()
    {
        auto combo_xBins = make_resolution_combo(Histo1DMinBits, Histo1DMaxBits, Histo1DDefBits);
        select_by_resolution(combo_xBins, m_sink->m_bins);

        QDialog dialog(m_q);
        auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        auto layout = new QFormLayout(&dialog);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->addRow(QSL("X Resolution"), combo_xBins);
        layout->addRow(buttonBox);

        if (dialog.exec() == QDialog::Accepted)
        {
            if (m_sink->m_bins != combo_xBins->currentData().toInt())
            {
                AnalysisPauser pauser(m_serviceProvider);
                m_sink->m_bins = combo_xBins->currentData().toInt();
                m_sink->setResolutionReductionFactor(Histo1D::NoRR);
                m_serviceProvider->analysisOperatorEdited(m_sink);

                qDebug() << __PRETTY_FUNCTION__ << "setting rrSlider to max";
                m_rrSlider->setMaximum(std::log2(getCurrentHisto()->getNumberOfBins()));
                m_rrSlider->setValue(m_rrSlider->maximum());
            }
        }
    }

    void displayChanged();
    void updateStatistics(u32 rrf);
    void updateAxisScales();
    bool yAxisIsLog();
    bool yAxisIsLin();
    void updateCursorInfoLabel(u32 rrf);
    void calibApply();
    void calibFillMax();
    void calibResetToFilter();
    void onRRSliderValueChanged(int sliderValue);
    void exportPlot();
    void exportPlotToClipboard();
    void saveHistogram();
    void onActionHistoListStats();

    Histo1DPtr getCurrentHisto()
    {
        return m_histos.value(m_histoIndex, {});
    }

    ~Histo1DWidgetPrivate()
    {
        qDebug() << __PRETTY_FUNCTION__ << this;
    }
};

Histo1DWidget::Histo1DWidget(const Histo1DPtr &histo, QWidget *parent)
    : Histo1DWidget(HistoList{ histo }, parent)
{
}

Histo1DWidget::Histo1DWidget(const HistoList &histos, QWidget *parent)
    : IPlotWidget(parent)
    , m_d(std::make_unique<Histo1DWidgetPrivate>())
{
    assert(!histos.isEmpty());

    m_d->m_q = this;
    m_d->m_histos = histos;
    m_d->m_plotHisto = new QwtPlotHistogram;
    m_d->m_replotTimer = new QTimer(this);
    m_d->m_cursorPosition = { make_quiet_nan(), make_quiet_nan() };
    m_d->m_plot = new QwtPlot;
    m_d->m_histoSpin = new QSpinBox(this);
    m_d->m_histoSpin->setMaximum(histos.size() - 1);
    set_widget_font_pointsize_relative(m_d->m_histoSpin, -2);

    m_d->m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());
    m_d->m_zoomer->setObjectName("zoomer");

    m_d->m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_d->m_zoomer->setZoomBase();

    DO_AND_ASSERT(connect(m_d->m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(onZoomerZoomed(const QRectF &))));

    DO_AND_ASSERT(connect(m_d->m_zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SIGNAL(zoomerZoomed(const QRectF &))));

    DO_AND_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &Histo1DWidget::mouseCursorMovedToPlotCoord));

    DO_AND_ASSERT(connect(m_d->m_zoomer, &ScrollZoomer::mouseCursorLeftPlot,
                       this, &Histo1DWidget::mouseCursorLeftPlot));

    DO_AND_ASSERT(connect(m_d->getCurrentHisto().get(), &Histo1D::axisBinningChanged,
                       this, [this] (Qt::Axis) {
        // Handle axis changes by zooming out fully. This will make sure
        // possible axis scale changes are immediately visible and the zoomer
        // is in a clean state.
        m_d->m_zoomer->setZoomStack(QStack<QRectF>(), -1);
        m_d->m_zoomer->zoom(0);
        replot();
    }));

    DO_AND_ASSERT(connect(m_d->m_plot->axisWidget(QwtPlot::xBottom), SIGNAL(scaleDivChanged()),
                          this, SLOT(onPlotBottomScaleDivChanged())));

    // Toolbar and actions
    m_d->m_toolBar = new QToolBar();
    auto tb = m_d->m_toolBar;
    {
        tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tb->setIconSize(QSize(16, 16));
        set_widget_font_pointsize(tb, 7);
    }

    QAction *action = nullptr;

    // Y-Scale Selection
    {
        m_d->m_yScaleCombo = new QComboBox;
        auto yScaleCombo = m_d->m_yScaleCombo;

        yScaleCombo->addItem(QSL("Lin"), static_cast<int>(AxisScaleType::Linear));
        yScaleCombo->addItem(QSL("Log"), static_cast<int>(AxisScaleType::Logarithmic));

        connect(yScaleCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                this, [this]() { m_d->displayChanged(); });

        tb->addWidget(make_vbox_container(QSL("Y-Scale"), yScaleCombo, 2, -2)
                      .container.release());
    }

    // Zoom action for easy enabling/disabling of the zoomer.
    m_d->m_actionZoom = new QAction(QIcon(":/resources/magnifier-zoom.png"), "Zoom", this);
    m_d->m_actionZoom->setCheckable(true);
    m_d->m_actionZoom->setChecked(true);
    m_d->m_actionZoom->setObjectName("zoomAction");
    connect(m_d->m_actionZoom, &QAction::toggled,
            m_d->m_zoomer, [this] (bool checked) {
                m_d->m_zoomer->setEnabled(checked);
            });
    // Optional: add the zoomer to the toolbar. Right now the action is not
    // visible but activated by, e.g. the rate estimation picker after two
    // points have been picked.
    tb->addAction(m_d->m_actionZoom);

    m_d->m_actionGaussFit = tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("Gauss"));
    m_d->m_actionGaussFit->setCheckable(true);
    connect(m_d->m_actionGaussFit, &QAction::toggled, this, &Histo1DWidget::on_tb_gauss_toggled);

    m_d->m_actionRateEstimation = tb->addAction(QIcon(":/generic_chart_with_pencil.png"),
                                                QSL("Rate Est."));
    m_d->m_actionRateEstimation->setStatusTip(QSL("Rate Estimation"));
    m_d->m_actionRateEstimation->setCheckable(true);
    connect(m_d->m_actionRateEstimation, &QAction::toggled,
            this, &Histo1DWidget::on_tb_rate_toggled);

    tb->addAction(QIcon(":/clear_histos.png"), QSL("Clear"), this, [this]() {
        if (auto histo = m_d->getCurrentHisto())
        {
            histo->clear();
            replot();
        }
    });

    // export plot to file / clipboard
    {
        auto menu = new QMenu(this);
        menu->addAction(QSL("to file"), this, [this] { m_d->exportPlot(); });
        menu->addAction(QSL("to clipboard"), this, [this] { m_d->exportPlotToClipboard(); });

        auto button = make_toolbutton(QSL(":/document-pdf.png"), QSL("Export"));
        button->setStatusTip(QSL("Export plot to file or clipboard"));
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);

        tb->addWidget(button);
    }

    action = tb->addAction(QIcon(":/document-save.png"), QSL("Save"),
                           this, [this] { m_d->saveHistogram(); });
    action->setStatusTip(QSL("Save the histogram to a text file"));

    m_d->m_actionSubRange = tb->addAction(QIcon(":/histo_subrange.png"), QSL("Subrange"),
                                          this, &Histo1DWidget::on_tb_subRange_clicked);
    m_d->m_actionSubRange->setStatusTip(QSL("Limit the histogram to a specific X-Axis range"));
    m_d->m_actionSubRange->setEnabled(false);

    m_d->m_actionChangeRes = tb->addAction(QIcon(":/histo_resolution.png"), QSL("Resolution"),
                                           this, [this]() { m_d->onActionChangeResolution(); });
    m_d->m_actionChangeRes->setStatusTip(QSL("Change histogram resolution"));
    m_d->m_actionChangeRes->setEnabled(false);

    m_d->m_actionCalibUi = tb->addAction(QIcon(":/operator_calibration.png"), QSL("Calibration"));
    m_d->m_actionCalibUi->setCheckable(true);
    m_d->m_actionCalibUi->setVisible(false);
    m_d->m_actionCalibUi->setStatusTip(QSL("Edit the histograms input calibration"));
    connect(m_d->m_actionCalibUi, &QAction::toggled,
            this, [this](bool b) { m_d->setCalibUiVisible(b); });

    m_d->m_actionInfo = tb->addAction(QIcon(":/info.png"), QSL("Info"));
    m_d->m_actionInfo->setCheckable(true);

    connect(m_d->m_actionInfo, &QAction::toggled, this, [this](bool b) {
        /* I did not manage to get the statusbar to resize to the smallest
         * possible size after hiding m_infoContainer. I tried
         * - setSizeConstraint(QLayout::SetFixedSize) on the container layout and on the statusbar layout
         * - calling adjustSize() on container and statusbar
         * but neither did resize the statusbar once the container was hidden.
         *
         * The quick fix is to hide/show the containers children explicitly.
         */
        for (auto childWidget: m_d->m_infoContainer->findChildren<QWidget *>())
        {
            childWidget->setVisible(b);
        }
    });

    // Resolution Reduction
    {
        m_d->m_rrSlider = make_res_reduction_slider();
        set_widget_font_pointsize_relative(m_d->m_rrSlider, -2);
        //m_d->m_rrSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
        auto boxStruct = make_vbox_container(QSL("Res"), m_d->m_rrSlider, 0, -2);
        m_d->m_rrLabel = boxStruct.label;
        set_widget_font_pointsize_relative(m_d->m_rrLabel, -2);
        tb->addWidget(boxStruct.container.release());

        connect(m_d->m_rrSlider, &QSlider::valueChanged, this, [this] (int sliderValue) {
            m_d->onRRSliderValueChanged(sliderValue);
        });
    }

    m_d->m_actionHistoListStats = tb->addAction(
        QIcon(QSL(":/document-text.png")),
        QSL("Statistics"));

    connect(m_d->m_actionHistoListStats, &QAction::triggered,
            this, [this] () { m_d->onActionHistoListStats(); });

    auto actionIntervalConditions = tb->addAction(QIcon(":/scissors.png"), "Interval Conditions");
    actionIntervalConditions->setObjectName("intervalConditions");
    actionIntervalConditions->setCheckable(true);
    m_d->m_actionConditions = actionIntervalConditions;
    m_d->m_actionConditions->setEnabled(false); // enabled when setSink() is called

    connect(actionIntervalConditions, &QAction::toggled,
            this, [this, actionIntervalConditions] (bool on) {
                if (on && !m_d->m_intervalConditionEditorController)
                {
                    m_d->m_intervalConditionEditorController =
                        new analysis::ui::IntervalConditionEditorController(
                            getSink(),
                            this, // histoWidget
                            getServiceProvider(),
                            this); // parent

                    m_d->m_intervalConditionEditorController->setObjectName(
                        "intervalConditionEditorController");

                    connect(m_d->m_intervalConditionEditorController->getDialog(), &QDialog::accepted,
                            this, [actionIntervalConditions] ()
                            {
                                actionIntervalConditions->setChecked(false);
                            });

                    connect(m_d->m_intervalConditionEditorController->getDialog(), &QDialog::rejected,
                            this, [actionIntervalConditions] ()
                            {
                                actionIntervalConditions->setChecked(false);
                            });
                }

                if (m_d->m_intervalConditionEditorController)
                {
                    if (!on && m_d->m_intervalConditionEditorController->hasUnsavedChanges())
                    {
                        auto choice = QMessageBox::warning(this, "Discarding condition changes",
                            "There are unsaved condition changes! Continue?",
                            QMessageBox::Cancel | QMessageBox::Ok,
                            QMessageBox::Cancel);

                        if (choice == QMessageBox::Cancel)
                        {
                            QSignalBlocker sb(actionIntervalConditions);
                            actionIntervalConditions->setChecked(true);
                            return;
                        }
                    }

                    m_d->m_intervalConditionEditorController->setEnabled(on);
                }
            });

    auto actionShowDependencyGraph = tb->addAction(QIcon(":/node-select.png"), "Dependency Graph");
    connect(actionShowDependencyGraph, &QAction::triggered,
            this, [this]
            {
                if (auto asp = getServiceProvider())
                    analysis::graph::show_dependency_graph(asp, getSink());
            });

#ifndef QT_NO_DEBUG
    auto combo_histoStyle = new QComboBox;
    combo_histoStyle->addItem("Outline", QwtPlotHistogram::Outline);
    combo_histoStyle->addItem("Columns", QwtPlotHistogram::Columns);
    combo_histoStyle->addItem("Lines", QwtPlotHistogram::Lines);
    tb->addWidget(combo_histoStyle);
    connect(combo_histoStyle, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, combo_histoStyle]
            {
                 m_d->histoStyle_ = combo_histoStyle->currentData().toInt();
                 replot();
            });
#endif

    // Final, right-side spacer. The listwidget adds the histo selection spinbox after
    // this.
    tb->addWidget(make_spacer_widget());

    // Setup the plot

    m_d->m_plotHisto->setStyle(QwtPlotHistogram::Outline);
    m_d->m_plotHisto->attach(m_d->m_plot);

    m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");

    connect(m_d->m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_d->m_replotTimer->start(ReplotPeriod_ms);

    m_d->m_plot->canvas()->setMouseTracking(true);


    //
    // Global stats box
    //
    m_d->m_globalStatsText = make_qwt_text_box(Qt::AlignTop | Qt::AlignRight);
    m_d->m_globalStatsTextItem = new mvme_qwt::TextLabelItem();
    m_d->m_globalStatsTextItem->setZ(PlotTextLayerZ);
    /* Margin added to contentsMargins() of the canvas. This is (mis)used to
     * not clip the top scrollbar. */
    //m_d->m_globalStatsTextItem->setMargin(25);

    //
    // Gauss stats box
    //
    m_d->m_gaussStatsText = make_qwt_text_box(Qt::AlignTop | Qt::AlignRight);

    m_d->m_gaussStatsTextItem = new mvme_qwt::TextLabelItem();
    m_d->m_gaussStatsTextItem->setVisible(false);
    m_d->m_gaussStatsTextItem->setZ(PlotTextLayerZ);

    m_d->m_textLabelLayout.addTextLabel(m_d->m_globalStatsTextItem);
    m_d->m_textLabelLayout.addTextLabel(m_d->m_gaussStatsTextItem);
    m_d->m_textLabelLayout.attachAll(m_d->m_plot);

    //
    // Calib Ui
    //
    m_d->m_calibUi.actual1 = new QDoubleSpinBox;
    m_d->m_calibUi.actual2 = new QDoubleSpinBox;
    m_d->m_calibUi.target1 = new QDoubleSpinBox;
    m_d->m_calibUi.target2 = new QDoubleSpinBox;
    m_d->m_calibUi.applyButton = new QPushButton(QSL("Apply"));
    m_d->m_calibUi.fillMaxButton = new QPushButton(QSL("Vis. Max"));
    m_d->m_calibUi.fillMaxButton->setToolTip(QSL("Fill the last focused actual value with the visible maximum histogram value"));

    /* FIXME: disabled due to Calibration not storing "global values" anymore
     * and thus resetting will reset to the input parameters (lowerLimit,
     * upperLimit[ interval. */
#if 0
    m_d->m_calibUi.resetToFilterButton = new QPushButton(QSL("Restore"));
    m_d->m_calibUi.resetToFilterButton->setToolTip(QSL("Restore base unit values from source calibration"));
#endif
    m_d->m_calibUi.closeButton = new QPushButton(QSL("Close"));

    m_d->m_calibUi.lastFocusedActual = m_d->m_calibUi.actual2;
    m_d->m_calibUi.actual1->installEventFilter(this);
    m_d->m_calibUi.actual2->installEventFilter(this);

    connect(m_d->m_calibUi.applyButton, &QPushButton::clicked,
            this, [this] { m_d->calibApply(); });
    connect(m_d->m_calibUi.fillMaxButton, &QPushButton::clicked,
            this, [this] { m_d->calibFillMax(); });

    //connect(m_d->m_calibUi.resetToFilterButton, &QPushButton::clicked, this, &Histo1DWidget::calibResetToFilter);
    connect(m_d->m_calibUi.closeButton, &QPushButton::clicked,
            this, [this]() { m_d->m_calibUi.window->reject(); });

    QVector<QDoubleSpinBox *> spins = {
        m_d->m_calibUi.actual1, m_d->m_calibUi.actual2,
        m_d->m_calibUi.target1, m_d->m_calibUi.target2
    };

    for (auto spin: spins)
    {
        spin->setDecimals(4);
        spin->setSingleStep(0.0001);
        spin->setMinimum(std::numeric_limits<double>::lowest());
        spin->setMaximum(std::numeric_limits<double>::max());
        spin->setValue(0.0);
    }

    m_d->m_calibUi.window = new QDialog(this);
    m_d->m_calibUi.window->setWindowTitle(QSL("Adjust Calibration"));
    connect(m_d->m_calibUi.window, &QDialog::rejected, this, [this]() {
        m_d->m_actionCalibUi->setChecked(false);
    });

    {
        auto font = m_d->m_calibUi.window->font();
        font.setPointSize(7);
        m_d->m_calibUi.window->setFont(font);
    }

    auto calibLayout = new QGridLayout(m_d->m_calibUi.window);
    calibLayout->setContentsMargins(3, 3, 3, 3);
    calibLayout->setSpacing(2);

    calibLayout->addWidget(new QLabel(QSL("Actual")), 0, 0, Qt::AlignHCenter);
    calibLayout->addWidget(new QLabel(QSL("Target")), 0, 1, Qt::AlignHCenter);

    calibLayout->addWidget(m_d->m_calibUi.actual1, 1, 0);
    calibLayout->addWidget(m_d->m_calibUi.target1, 1, 1);

    calibLayout->addWidget(m_d->m_calibUi.actual2, 2, 0);
    calibLayout->addWidget(m_d->m_calibUi.target2, 2, 1);

    calibLayout->addWidget(m_d->m_calibUi.fillMaxButton, 3, 0, 1, 1);
    calibLayout->addWidget(m_d->m_calibUi.applyButton, 3, 1, 1, 1);

    //calibLayout->addWidget(m_d->m_calibUi.resetToFilterButton, 4, 0, 1, 1);
    calibLayout->addWidget(m_d->m_calibUi.closeButton, 4, 1, 1, 1);

    //
    // Rate Estimation
    //
    m_d->m_rateEstimationData.visible = false;

    auto make_position_marker = [](QwtPlot *plot)
    {
        auto marker = new QwtPlotMarker;
        marker->setLabelAlignment( Qt::AlignLeft | Qt::AlignBottom );
        marker->setLabelOrientation( Qt::Vertical );
        marker->setLineStyle( QwtPlotMarker::VLine );
        marker->setLinePen( Qt::black, 0, Qt::DashDotLine );
        marker->setZ(PlotTextLayerZ);
        marker->attach(plot);
        marker->hide();
        return marker;
    };

    m_d->m_rateEstimationX1Marker = make_position_marker(m_d->m_plot);
    m_d->m_rateEstimationX2Marker = make_position_marker(m_d->m_plot);

    m_d->m_rateEstimationFormulaMarker = new QwtPlotMarker;
    m_d->m_rateEstimationFormulaMarker->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    m_d->m_rateEstimationFormulaMarker->setZ(PlotTextLayerZ);
    m_d->m_rateEstimationFormulaMarker->attach(m_d->m_plot);
    m_d->m_rateEstimationFormulaMarker->hide();

    m_d->m_rateEstimationPointPicker = new QwtPlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                                               QwtPicker::VLineRubberBand, QwtPicker::ActiveOnly,
                                               m_d->m_plot->canvas());
    QPen pickerPen(Qt::red);
    m_d->m_rateEstimationPointPicker->setTrackerPen(pickerPen);
    m_d->m_rateEstimationPointPicker->setRubberBandPen(pickerPen);
    m_d->m_rateEstimationPointPicker->setStateMachine(new AutoBeginClickPointMachine);
    m_d->m_rateEstimationPointPicker->setEnabled(false);

    DO_AND_ASSERT(connect(m_d->m_rateEstimationPointPicker, SIGNAL(selected(const QPointF &)),
                       this, SLOT(on_ratePointerPicker_selected(const QPointF &))));

    m_d->m_rateEstimationCurve = make_plot_curve(Qt::red, 2.0, PlotAdditionalCurvesLayerZ, m_d->m_plot);
    m_d->m_rateEstimationCurve->hide();

    //
    // Gauss Curve
    //
    m_d->m_gaussCurve = make_plot_curve(Qt::green, 2.0, PlotAdditionalCurvesLayerZ, m_d->m_plot);
    m_d->m_gaussCurve->hide();

    //
    // Watermark text when exporting
    //
    {
        m_d->m_waterMarkText = QwtText();
        m_d->m_waterMarkText.setRenderFlags(Qt::AlignRight | Qt::AlignBottom);
        m_d->m_waterMarkText.setColor(QColor(0x66, 0x66, 0x66, 0x40));

        QFont font;
        font.setPixelSize(16);
        font.setBold(true);
        m_d->m_waterMarkText.setFont(font);

        m_d->m_waterMarkText.setText(QString("mvme-%1").arg(GIT_VERSION_TAG));

        m_d->m_waterMarkLabel = new QwtPlotTextLabel;
        m_d->m_waterMarkLabel->setMargin(10);
        m_d->m_waterMarkLabel->setText(m_d->m_waterMarkText);
        m_d->m_waterMarkLabel->attach(m_d->m_plot);
        m_d->m_waterMarkLabel->hide();
    }

    //
    // Histo selection spinbox
    //
    connect(m_d->m_histoSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &Histo1DWidget::onHistoSpinBoxValueChanged);

    m_d->m_toolBar->addWidget(make_vbox_container(QSL("Histogram #"),
                                                  m_d->m_histoSpin, 2, -2)
                      .container.release());

    //
    // StatusBar and info widgets
    //
    m_d->m_statusBar = make_statusbar();

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

    resize(1000, 562);
    selectHistogram(0);
}

Histo1DWidget::~Histo1DWidget()
{
    delete m_d->m_plotHisto;
    if (m_d->histoStatsWidget_)
        m_d->histoStatsWidget_->close();
}

void Histo1DWidget::setHistogram(const Histo1DPtr &histo)
{
    m_d->m_histos = { histo };
    m_d->m_calib = {};
    m_d->m_sink = {};
    m_d->m_sinkModifiedCallback = {};
    m_d->m_actionSubRange->setEnabled(false);
    m_d->m_actionChangeRes->setEnabled(false);
    m_d->m_actionCalibUi->setVisible(false);
    selectHistogram(0);
}

void Histo1DWidgetPrivate::updateAxisScales()
{
    double minValue = m_stats.minValue;
    double maxValue = m_stats.maxValue;

    if (yAxisIsLog())
    {
        // Force log scale to go from [1.0, something >= 1.0).
        minValue = 1.0;
        maxValue = std::max(minValue, maxValue);
    }

    // Scale the y-axis by 5% to have some margin to the top and bottom of the
    // widget. Mostly to make the top scrollbar not overlap the plotted graph.
    minValue *= (minValue < 0.0) ? 1.05 : 0.95;
    maxValue *= (maxValue < 0.0) ? 0.95 : 1.05;

    //qDebug() << "y scale min" << minValue << "max" << maxValue;

    // This sets a fixed y-axis scale effectively overriding any changes made by
    // the scrollzoomer.
    m_plot->setAxisScale(QwtPlot::yLeft, minValue, maxValue);

    // xAxis
    if (m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        m_plot->setAxisScale(QwtPlot::xBottom,
                             getCurrentHisto()->getXMin(),
                             getCurrentHisto()->getXMax());
        m_zoomer->setZoomBase();
    }

    m_plotHisto->setBaseline(minValue);
    m_plot->updateAxes();
}

void Histo1DWidget::replot()
{
    if (!m_d->getCurrentHisto())
        return;

    m_d->m_plotHisto->setStyle(static_cast<QwtPlotHistogram::HistogramStyle>(m_d->histoStyle_));

    // ResolutionReduction
    const u32 rrf = m_d->m_rrf;

    m_d->updateStatistics(rrf);
    m_d->updateAxisScales();
    m_d->updateCursorInfoLabel(rrf);

    m_d->m_plotHistoData->setResolutionReductionFactor(rrf);
    m_d->m_plotRateEstimationData->setResolutionReductionFactor(rrf);

    auto xBinning = m_d->getCurrentHisto()->getAxisBinning(Qt::XAxis);

    // update histo info label
    auto infoText = QString("Underflow:  %1\n"
                            "Overflow:   %2\n"
                            "VisBins:    %3\n"
                            "BinWidth:   %4\n"
                            "Bins/Units: %5\n"
                            "PhysBins:   %6\n"
                            )
        .arg(m_d->getCurrentHisto()->getUnderflow())
        .arg(m_d->getCurrentHisto()->getOverflow())
        .arg(xBinning.getBins(rrf))
        .arg(xBinning.getBinWidth(rrf))
        .arg(xBinning.getBinsToUnitsRatio(rrf))
        .arg(xBinning.getBins());
        ;

    m_d->m_labelHistoInfo->setText(infoText);

    // rate and efficiency estimation
    if (m_d->m_rateEstimationData.visible)
    {
        /* This code tries to interpolate the exponential function formed by
         * the two selected data points. */

        auto xy1  = m_d->getCurrentHisto()->getValueAndBinLowEdge(m_d->m_rateEstimationData.x1, rrf);
        auto xy2  = m_d->getCurrentHisto()->getValueAndBinLowEdge(m_d->m_rateEstimationData.x2, rrf);

        double x1 = xy1.first;
        double y1 = xy1.second;
        double x2 = xy2.first;
        double y2 = xy2.second;

        double tau      = (x2 - x1) / log(y1 / y2);
        double freeRate = 1.0 / tau; // 1/x-axis unit

        double nom      = m_d->getCurrentHisto()->calcStatistics(x1, x2, rrf).entryCount;
        double denom    = ( (pow(E1, -x1/tau) - pow(E1, -x2/tau)));
        double factor   = (1.0 - pow(E1, -x1/tau));

        double norm                 = nom / denom;
        double freeCounts_0_x1      = norm * factor;
        double histoCounts_0_x1     = m_d->getCurrentHisto()->calcStatistics(0.0, x1, rrf).entryCount;
        double freeCounts_x1_inf    = norm * pow(E1, -x1 / tau);
        double freeCounts_0_inf     = norm;
        double efficiency           = (histoCounts_0_x1 + freeCounts_x1_inf) / freeCounts_0_inf;

        // Same values as used in RateEstimationCurveData but printed here to avoid spamming
        double norm_fitCurve          = y1 / (( 1.0 / tau) * pow(E1, -x1 / tau));
        double norm_fitCurve_adjusted =
            norm_fitCurve * xBinning.getBinsToUnitsRatio(rrf);

#if 0
        qDebug() << __PRETTY_FUNCTION__ << endl
            << "run =" << m_d->m_context->getRunInfo().runId << endl
            << "  x1,y1 =" << x1 << y1 << endl
            << "  x2,y2 =" << x2 << y2 << endl
            << "  tau   =" << tau << endl
            << "  freeRate (1/tau)   =" << freeRate << endl
            << "  nom (counts x1_x2) =" << nom << endl
            << "  denom  =" << denom << endl
            << "  factor =" << factor << endl
            << "  norm = freeCounts_0_inf =" << norm << endl
            << "  freeCounts_0_x1   =" << freeCounts_0_x1 << endl
            << "  histoCounts_0_x1  =" << histoCounts_0_x1 << endl
            << "  histoCounts_total ="
            << m_d->getCurrentHisto()->calcStatistics(m_d->getCurrentHisto()->getXMin(),
                                            m_d->getCurrentHisto()->getXMax(),
                                            rrf).entryCount
            << endl
            << "  efficiency        =" << efficiency << endl
            << endl
            << "  norm_fitCurve          =" << norm_fitCurve << endl
            << "  norm_fitCurve_adjusted =" << norm_fitCurve_adjusted
            ;
#else
        (void) freeCounts_0_x1;
        (void) norm_fitCurve_adjusted;
#endif

#if 0
        infoText += QString("\n"
                            "(x1, y1)=(%1, %2)\n"
                            "(x2, y2)=(%3, %4)\n"
                            "tau=%5, c=%6, c_norm=%11\n"
                            "FR=%7, FC=%8, HC=%9\n"
                            "efficiency=%10")
            .arg(x1)
            .arg(y1)
            .arg(x2)
            .arg(y2)
            .arg(tau)
            .arg(c)
            .arg(freeRate)
            .arg(freeCounts)
            .arg(histoCounts)
            .arg(efficiency)
            .arg(c_norm)
            ;
#endif

        QString markerText;

        if (!std::isnan(tau) && !std::isnan(efficiency))
        {
            auto unitX = m_d->getCurrentHisto()->getAxisInfo(Qt::XAxis).unit;
            if (unitX.isEmpty())
                unitX = QSL("x");

            markerText = QString(QSL("freeRate=%1 <sup>1</sup>&frasl;<sub>%2</sub>; eff=%3")
                                 .arg(freeRate, 0, 'g', 4)
                                 .arg(unitX)
                                 .arg(efficiency, 0, 'g', 4)
                                );
        }
        else
        {
            markerText = QSL("");
        }

        QwtText rateFormulaText(markerText, QwtText::RichText);
        auto font = rateFormulaText.font();
        font.setPointSize(font.pointSize() + 1);
        rateFormulaText.setFont(font);
        m_d->m_rateEstimationFormulaMarker->setXValue(x1);

        /* The goal is to draw the marker at 0.9 of the plot height. Doing this
         * in plot coordinates does work for a linear y-axis scale but
         * positions the text way too high for a logarithmic scale. Instead of
         * using plot coordinates directly we're using 0.9 of the canvas height
         * and transform that pixel coordinate to a plot coordinate.
         */
        s32 canvasHeight = m_d->m_plot->canvas()->height();
        s32 pixelY = canvasHeight - canvasHeight * 0.9;
        static const s32 minPixelY = 50;
        if (pixelY < minPixelY)
            pixelY = minPixelY;
        double plotY = m_d->m_plot->canvasMap(QwtPlot::yLeft).invTransform(pixelY);

        m_d->m_rateEstimationFormulaMarker->setYValue(plotY);
        m_d->m_rateEstimationFormulaMarker->setLabel(rateFormulaText);
        m_d->m_rateEstimationFormulaMarker->show();

        //qDebug() << __PRETTY_FUNCTION__ << "rate estimation formula marker x,y =" << x1 << plotY;
    }

    // window and axis titles. If no sink is set the window title is left
    // untouched, so can be set externally, e.g. when creating the widget.
    if (auto sink = getSink())
    {
        auto pathParts = analysis::make_parent_path_list(sink);
        pathParts.push_back(m_d->getCurrentHisto()->objectName());
        setWindowTitle(pathParts.join('/'));
    }

    auto axisInfo = m_d->getCurrentHisto()->getAxisInfo(Qt::XAxis);
    m_d->m_plot->axisWidget(QwtPlot::xBottom)->setTitle(make_title_string(axisInfo));

    m_d->m_plot->replot();

#if 0
    // prints plot item pointers and their z value
    for (auto item: m_d->m_plot->itemList())
    {
        qDebug() << __PRETTY_FUNCTION__ << item << item->z();
    }
#endif
}

void Histo1DWidgetPrivate::displayChanged()
{
    auto scaleType = static_cast<AxisScaleType>(m_yScaleCombo->currentData().toInt());

    if (scaleType == AxisScaleType::Linear && !yAxisIsLin())
    {
        m_plot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        m_plot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (scaleType == AxisScaleType::Logarithmic && !yAxisIsLog())
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        m_plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
    }

    m_q->replot();
}

void Histo1DWidget::onZoomerZoomed(const QRectF &)
{
    if (m_d->m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        m_d->m_plot->setAxisScale(QwtPlot::xBottom,
                                  m_d->getCurrentHisto()->getXMin(), m_d->getCurrentHisto()->getXMax());
        m_d->m_plot->replot();
        m_d->m_zoomer->setZoomBase();
    }

    // do not zoom outside the histogram range
    auto scaleDiv = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom);
    double lowerBound = scaleDiv.lowerBound();
    double upperBound = scaleDiv.upperBound();

    if (lowerBound <= upperBound)
    {
        if (lowerBound < m_d->getCurrentHisto()->getXMin())
        {
            scaleDiv.setLowerBound(m_d->getCurrentHisto()->getXMin());
        }

        if (upperBound > m_d->getCurrentHisto()->getXMax())
        {
            scaleDiv.setUpperBound(m_d->getCurrentHisto()->getXMax());
        }
    }
    else
    {
        if (lowerBound > m_d->getCurrentHisto()->getXMin())
        {
            scaleDiv.setLowerBound(m_d->getCurrentHisto()->getXMin());
        }

        if (upperBound < m_d->getCurrentHisto()->getXMax())
        {
            scaleDiv.setUpperBound(m_d->getCurrentHisto()->getXMax());
        }
    }

    m_d->m_plot->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);

    replot();
}

void Histo1DWidget::mouseCursorMovedToPlotCoord(QPointF pos)
{
    m_d->m_cursorPosition = pos;
    m_d->updateCursorInfoLabel(m_d->m_rrf);
}

void Histo1DWidget::mouseCursorLeftPlot()
{
    m_d->m_cursorPosition = QPointF(make_quiet_nan(), make_quiet_nan());
    m_d->updateCursorInfoLabel(m_d->m_rrf);
}

void Histo1DWidgetPrivate::updateStatistics(u32 rrf)
{
    if (!getCurrentHisto())
        return;

    double lowerBound = m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double upperBound = m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();

    //
    // global stats
    //
    m_stats = getCurrentHisto()->calcStatistics(lowerBound, upperBound, rrf);

    static const QString globalStatsTemplate = QSL(
        "<table>"
        "<tr><td align=\"left\">Mean   </td><td>%L1</td></tr>"
        "<tr><td align=\"left\">RMS    </td><td>%L2</td></tr>"
        "<tr><td align=\"left\">Max    </td><td>%L3</td></tr>"
        "<tr><td align=\"left\">Max Y  </td><td>%L4</td></tr>"
        "<tr><td align=\"left\">Counts </td><td>%L5</td></tr>"
        "</table>"
        );

    double maxBinCenter = ((m_stats.entryCount > 0)
                           ? getCurrentHisto()->getBinCenter(m_stats.maxBin, rrf)
                           : 0.0);

    static const int fieldWidth = 0;

    QString buffer = globalStatsTemplate
        .arg(m_stats.mean, fieldWidth)
        .arg(m_stats.sigma, fieldWidth)
        .arg(maxBinCenter, fieldWidth)
        .arg(m_stats.maxValue, fieldWidth)
        .arg(m_stats.entryCount, fieldWidth, 'f', 0)
        ;

    m_globalStatsText->setText(buffer, QwtText::RichText);
    m_globalStatsTextItem->setText(*m_globalStatsText);

    //
    // set updated histo stats on the gauss curve object
    //
    auto gaussCurveData = reinterpret_cast<Histo1DGaussCurveData *>(m_gaussCurve->data());
    gaussCurveData->setStats(m_stats);

    static const double Sqrt2Pi = std::sqrt(2 * M_PI);

    double a = m_stats.maxValue;
    double s = m_stats.fwhm / FWHMSigmaFactor;
    double scaleFactor = getCurrentHisto()->getAxisBinning(Qt::XAxis).getBinsToUnitsRatio(rrf);


    double thingsBelowGauss = a * s * Sqrt2Pi;
    // Scale with the max y value. This is the value of the maxBin
    thingsBelowGauss *= scaleFactor;

    static const QString gaussStatsTemplate = QSL(
        "<table>"
        "<tr><th>Gauss</th></tr>"
        "<tr><td align=\"left\">FWHM  </td><td>%L1</td></tr>"
        "<tr><td align=\"left\">Center</td><td>%L2</td></tr>"
        "<tr><td align=\"left\">Counts</td><td>%L3</td></tr>"
        "</table>"
        );

    buffer = gaussStatsTemplate
        .arg(m_stats.fwhm)
        .arg(m_stats.fwhmCenter)
        .arg(thingsBelowGauss)
        ;

    m_gaussStatsText->setText(buffer, QwtText::RichText);
    m_gaussStatsTextItem->setText(*m_gaussStatsText);
}

bool Histo1DWidgetPrivate::yAxisIsLog()
{
    return dynamic_cast<QwtLogScaleEngine *>(m_plot->axisScaleEngine(QwtPlot::yLeft));
}

bool Histo1DWidgetPrivate::yAxisIsLin()
{
    return dynamic_cast<QwtLinearScaleEngine *>(m_plot->axisScaleEngine(QwtPlot::yLeft));
}

void Histo1DWidgetPrivate::exportPlot()
{
    QString fileName = getCurrentHisto()->objectName();
    fileName.replace("/", "_");
    fileName.replace("\\", "_");
    fileName += QSL(".pdf");

    assert(m_serviceProvider);

    if (m_serviceProvider)
    {
        fileName = QDir(m_serviceProvider->getWorkspacePath(QSL("PlotsDirectory"))).filePath(fileName);
    }

    m_plot->setTitle(getCurrentHisto()->getTitle());

    QString footerString = getCurrentHisto()->getFooter();
    QwtText footerText(footerString);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_plot->setFooter(footerText);
    m_waterMarkLabel->show();

    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.exportTo(m_plot, fileName);

    m_plot->setTitle(QString());
    m_plot->setFooter(QString());
    m_waterMarkLabel->hide();
}

void Histo1DWidgetPrivate::exportPlotToClipboard()
{
    m_plot->setTitle(getCurrentHisto()->getTitle());

    QString footerString = getCurrentHisto()->getFooter();
    QwtText footerText(footerString);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_plot->setFooter(footerText);
    m_waterMarkLabel->show();

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
    renderer.renderTo(m_plot, image);

    m_plot->setTitle(QString());
    m_plot->setFooter(QString());
    m_waterMarkLabel->hide();

    auto clipboard = QApplication::clipboard();
    clipboard->clear();
    clipboard->setImage(image);
}

void Histo1DWidgetPrivate::saveHistogram()
{
    QString path = QSettings().value("Files/LastHistogramExportDirectory").toString();

    if (path.isEmpty())
    {
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);
    }

    auto name = getCurrentHisto()->objectName();

    QString fileName = QString("%1/%2.txt")
        .arg(path)
        .arg(name);

    QFileDialog fd(m_q, "Save Histogram", fileName, "Text Files (*.txt);; All Files (*.*)");
    fd.setDefaultSuffix(".txt");
    fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);
    fd.selectFile(fileName);

    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
        return;

    fileName = fd.selectedFiles().front();

    qDebug() << fileName;

    if (fileName.isEmpty())
        return;

    QFile outFile(fileName);
    if (!outFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0, "Error", QString("Error opening %1 for writing").arg(fileName));
        return;
    }

    QTextStream out(&outFile);
    writeHisto1D(out, getCurrentHisto().get());

    if (out.status() == QTextStream::Ok)
    {
        QSettings().setValue("Files/LastHistogramExportDirectory", QFileInfo(fileName).absolutePath());
    }
}

void Histo1DWidgetPrivate::updateCursorInfoLabel(u32 rrf)
{
    if (!getCurrentHisto())
        return;

    double plotX = m_cursorPosition.x();
    double plotY = m_cursorPosition.y();
    auto binning = getCurrentHisto()->getAxisBinning(Qt::XAxis);
    s64 binX = binning.getBin(plotX, rrf);

    QString text;

    if (!qIsNaN(plotX) && !qIsNaN(plotY) && binX >= 0)
    {
        double x = plotX;
        double y = getCurrentHisto()->getBinContent(binX, rrf);
        double binLowEdge = binning.getBinLowEdge((u32)binX, rrf);

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
            << QString("Y=%1").arg(getCurrentHisto()->getBinContent(binX))
            << QString("minX=%1, maxX=%2").arg(getCurrentHisto()->getXMin()).arg(getCurrentHisto()->getXMax());
        ;

        QString text = sl.join("\n");
#endif
    }

    // update the label which will calculate a new width
    m_labelCursorInfo->setText(text);

    // use the largest width and height the label ever had to stop the label from constantly changing its width
    if (m_labelCursorInfo->isVisible())
    {
        m_labelCursorInfoMaxWidth = std::max(m_labelCursorInfoMaxWidth,
                                             m_labelCursorInfo->width());
        m_labelCursorInfo->setMinimumWidth(m_labelCursorInfoMaxWidth);

        m_labelCursorInfo->setMinimumHeight(m_labelCursorInfoMaxHeight);
        m_labelCursorInfoMaxHeight = std::max(m_labelCursorInfoMaxHeight,
                                              m_labelCursorInfo->height());
    }
}

void Histo1DWidget::setServiceProvider(AnalysisServiceProvider *serviceProvider)
{
    m_d->m_serviceProvider = serviceProvider;
}

AnalysisServiceProvider *Histo1DWidget::getServiceProvider() const
{
    return m_d->m_serviceProvider;
}

void Histo1DWidgetPrivate::calibApply()
{
    Q_ASSERT(m_calib);
    Q_ASSERT(m_serviceProvider);

    using namespace analysis;

    double a1 = m_calibUi.actual1->value();
    double a2 = m_calibUi.actual2->value();
    double t1 = m_calibUi.target1->value();
    double t2 = m_calibUi.target2->value();

    if (a1 - a2 == 0.0 || t1 == t2)
        return;

    double a = (t1 - t2) / (a1 - a2);
    double b = t1 - a * a1;

    u32 address = m_histoIndex;

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

    m_calibUi.actual1->setValue(m_calibUi.target1->value());
    m_calibUi.actual2->setValue(m_calibUi.target2->value());

    AnalysisPauser pauser(m_serviceProvider);

    m_calib->setCalibration(address, targetMin, targetMax);
    m_serviceProvider->getAnalysis()->setOperatorEdited(m_calib);

    m_q->on_tb_rate_toggled(m_rateEstimationData.visible);
}

void Histo1DWidgetPrivate::calibResetToFilter()
{
    Q_ASSERT(m_calib);
    Q_ASSERT(m_serviceProvider);

    using namespace analysis;

    Pipe *inputPipe = m_calib->getSlot(0)->inputPipe;
    if (inputPipe)
    {
        Parameter *inputParam = inputPipe->getParameter(m_histoIndex);
        if (inputParam)
        {
            double minValue = inputParam->lowerLimit;
            double maxValue = inputParam->upperLimit;
            AnalysisPauser pauser(m_serviceProvider);
            m_calib->setCalibration(m_histoIndex, minValue, maxValue);
            analysis::do_beginRun_forward(m_calib.get());
        }
    }
}

void Histo1DWidgetPrivate::calibFillMax()
{
    // The values in m_stats are calculated in terms of the current res
    // reduction factor in replot(). This means m_stats.maxBin can be used
    // unmodified in the call to getBinLowEdge().
    double maxAt = getCurrentHisto()->getBinLowEdge(m_stats.maxBin, m_rrf);
    m_calibUi.lastFocusedActual->setValue(maxAt);
}

bool Histo1DWidget::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_d->m_calibUi.actual1 || watched == m_d->m_calibUi.actual2)
        && (event->type() == QEvent::FocusIn))
    {
        m_d->m_calibUi.lastFocusedActual = qobject_cast<QDoubleSpinBox *>(watched);
    }
    return QWidget::eventFilter(watched, event);
}

bool Histo1DWidget::event(QEvent *e)
{
    if (e->type() == QEvent::StatusTip)
    {
        m_d->m_statusBar->showMessage(reinterpret_cast<QStatusTipEvent *>(e)->tip());
        return true;
    }
    else if (e->type() == QEvent::Close)
    {
    }

    return QWidget::event(e);
}

void Histo1DWidgetPrivate::onRRSliderValueChanged(int sliderValue)
{
#if 0
    qDebug() << __PRETTY_FUNCTION__
        << "rrS min/max =" << m_rrSlider->minimum() << m_rrSlider->maximum()
        << ", new rrS value =" << sliderValue
        << ", current rrf =" << m_rrf
        ;
#endif

    u32 physBins = 0;
    u32 visBins  = 1u << sliderValue;

    if (auto histo = getCurrentHisto())
        physBins = histo->getNumberOfBins();

    // (phys number of bins) / (res reduction number of bins)
    m_rrf = physBins / visBins;

    m_rrLabel->setText(QSL("Res: %1, %2 bit")
                       .arg(visBins)
                       .arg(std::log2(visBins))
                      );

#if 0
    qDebug() << __PRETTY_FUNCTION__
        << "physBins =" << physBins
        << ", visBins =" << visBins
        << ", new rrf =" << m_rrf;
#endif

    if (m_sink)
    {
        //qDebug() << "  updating sink" << m_sink.get();

        m_sink->setResolutionReductionFactor(m_rrf);

        // FIXME: this code results in the analysis being marked modified on opening a
        // histogram for the first time as the rrf changes from 0 (NoRR) to 1 (physBins ==
        // visBins)
        // Also invoking the callback here rebuilds the analysis immediately. This is not
        // needed at all as the res reduction is a display only thing and doesn't interact
        // with the a2 runtime in any way.
#if 0
        if (m_sinkModifiedCallback)
        {
            qDebug() << "  invoking sinkModifiedCallback";
            m_sinkModifiedCallback(m_sink);
        }
#endif
    }

    m_q->replot();

    //qDebug() << __PRETTY_FUNCTION__ << "<<<<< end of function";
}

void Histo1DWidget::setSink(const SinkPtr &sink, HistoSinkCallback sinkModifiedCallback)
{
    Q_ASSERT(sink);

    m_d->m_sink = sink;
    m_d->m_sinkModifiedCallback = sinkModifiedCallback;
    m_d->m_actionSubRange->setEnabled(true);
    m_d->m_actionChangeRes->setEnabled(true);
    m_d->m_actionConditions->setEnabled(sink->getUserLevel() > 0);

    auto rrf = sink->getResolutionReductionFactor();

    if (rrf == Histo1D::NoRR)
    {
#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "setting rrSlider value to max" << m_d->m_rrSlider->maximum();
#endif

        m_d->m_rrSlider->setValue(m_d->m_rrSlider->maximum());
    }
    else
    {
        u32 visBins = m_d->getCurrentHisto()->getNumberOfBins(rrf);
        int sliderValue = std::log2(visBins);

#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "sink rrf =" << rrf
            << ", -> setting slider value to" << sliderValue;
#endif

        m_d->m_rrSlider->setValue(sliderValue);
    }

    replot();
}

Histo1DWidget::SinkPtr Histo1DWidget::getSink() const
{
    return m_d->m_sink;
}

void Histo1DWidget::setResolutionReductionFactor(u32 rrf)
{
    if (rrf == 0)
        rrf = 1;
    u32 physBins = m_d->getCurrentHisto()->getNumberOfBins();
    u32 visBins  = physBins / rrf;
    int sliderValue = std::log2(visBins);
    m_d->m_rrSlider->setValue(sliderValue);
}

void Histo1DWidget::setResolutionReductionSliderEnabled(bool b)
{
    m_d->m_rrSlider->setEnabled(b);
}

void Histo1DWidget::on_tb_subRange_clicked()
{
    Q_ASSERT(m_d->m_sink);

    double visibleMinX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();

    Histo1DSubRangeDialog dialog(m_d->m_sink, m_d->m_sinkModifiedCallback,
                                 visibleMinX, visibleMaxX, this);
    dialog.exec();
}

void Histo1DWidget::on_tb_rate_toggled(bool checked)
{
    if (checked)
    {
        m_d->m_rateEstimationData = RateEstimationData();
        #if 0
        // uncheck the currently active exclusive action
        if (auto action = m_d->m_exclusiveActions->checkedAction())
            action->setChecked(false);
        #endif
        // enable the picker
        m_d->m_rateEstimationPointPicker->setEnabled(true);
    }
    else
    {
        m_d->m_rateEstimationData.visible = false;
        m_d->m_rateEstimationX1Marker->hide();
        m_d->m_rateEstimationX2Marker->hide();
        m_d->m_rateEstimationFormulaMarker->hide();
        m_d->m_rateEstimationCurve->hide();

        // go back to zooming
        m_d->m_actionZoom->setChecked(true);
    }

    replot();
}

void Histo1DWidget::on_tb_gauss_toggled(bool checked)
{
    if (checked)
    {
        m_d->m_gaussCurve->show();
        m_d->m_gaussStatsTextItem->setVisible(true);
    }
    else
    {
        m_d->m_gaussCurve->hide();
        m_d->m_gaussStatsTextItem->setVisible(false);
    }

    replot();
}

void Histo1DWidget::on_tb_test_clicked()
{
}

void Histo1DWidget::on_ratePointerPicker_selected(const QPointF &pos)
{
    if (std::isnan(m_d->m_rateEstimationData.x1))
    {
        m_d->m_rateEstimationData.x1 = pos.x();

        double x1 = m_d->getCurrentHisto()->getValueAndBinLowEdge(
            m_d->m_rateEstimationData.x1, m_d->m_rrf).first;

        m_d->m_rateEstimationX1Marker->setXValue(m_d->m_rateEstimationData.x1);
        m_d->m_rateEstimationX1Marker->setLabel(QString("    x1=%1").arg(x1));
        m_d->m_rateEstimationX1Marker->show();
    }
    else if (std::isnan(m_d->m_rateEstimationData.x2))
    {
        m_d->m_rateEstimationData.x2 = pos.x();

        if (m_d->m_rateEstimationData.x1 > m_d->m_rateEstimationData.x2)
        {
            std::swap(m_d->m_rateEstimationData.x1, m_d->m_rateEstimationData.x2);
        }

        m_d->m_rateEstimationData.visible = true;
        // Disable rate point picking and go back to zooming
        m_d->m_rateEstimationPointPicker->setEnabled(false);
        m_d->m_actionZoom->setChecked(true);

        double x1 = m_d->getCurrentHisto()->getValueAndBinLowEdge(m_d->m_rateEstimationData.x1,
                                                                  m_d->m_rrf).first;

        double x2 = m_d->getCurrentHisto()->getValueAndBinLowEdge(m_d->m_rateEstimationData.x2,
                                                                  m_d->m_rrf).first;

        // set both x1 and x2 as they might have been swapped above
        m_d->m_rateEstimationX1Marker->setXValue(m_d->m_rateEstimationData.x1);
        m_d->m_rateEstimationX1Marker->setLabel(QString("    x1=%1").arg(x1));
        m_d->m_rateEstimationX2Marker->setXValue(m_d->m_rateEstimationData.x2);
        m_d->m_rateEstimationX2Marker->setLabel(QString("    x2=%1").arg(x2));
        m_d->m_rateEstimationX2Marker->show();
        m_d->m_rateEstimationCurve->show();
    }
    else
    {
        InvalidCodePath;
    }

    replot();
}

void Histo1DWidgetPrivate::onActionHistoListStats()
{
    if (!histoStatsWidget_)
    {
        auto statsWidget = new HistoStatsWidget;
        statsWidget->setAttribute(Qt::WA_DeleteOnClose);
        statsWidget->show();
        add_widget_close_action(statsWidget);
        (new WidgetGeometrySaver(statsWidget))->addAndRestore(
            statsWidget, QSL("WindowGeometries/HistoStatsWidget"));
        QObject::connect(statsWidget, &QObject::destroyed,
                         m_q, [this] { histoStatsWidget_ = nullptr; });
        histoStatsWidget_ = statsWidget;

        m_q->onPlotBottomScaleDivChanged(); // force an initial update of the stats range

        QString title;
        if (m_sink)
        {
            statsWidget->addSink(m_sink);
            title = QSL("Statistics for histogram array '%1'").arg(m_sink->objectName());
        }
        else if (auto histo = getCurrentHisto())
        {
            statsWidget->addHistogram(histo);
            title = QSL("Statistics for histogram '%1'").arg(histo->getTitle());
        }
        statsWidget->setWindowTitle(title);
        // Set the initial resolution for stats calculation based on the visible
        // resolution set here. Unlike zooming this is not currently synced.
        statsWidget->setEffectiveResolution(1u << m_rrSlider->value());
    }

    show_and_activate(histoStatsWidget_);
}

void Histo1DWidget::onPlotBottomScaleDivChanged()
{
    if (m_d->histoStatsWidget_)
    {
        m_d->histoStatsWidget_->setXScaleDiv(m_d->m_plot->axisScaleDiv(QwtPlot::xBottom));
    }
}

QwtPlot *Histo1DWidget::getPlot()
{
    return m_d->m_plot;
}

const QwtPlot *Histo1DWidget::getPlot() const
{
    return m_d->m_plot;
}

void Histo1DWidget::onHistoSpinBoxValueChanged(int index)
{
    selectHistogram(index);
}

void Histo1DWidget::selectHistogram(int index)
{
    if (0 <= index && index < m_d->m_histos.size())
    {
        m_d->m_histoIndex = index;

        assert(m_d->getCurrentHisto() == m_d->m_histos.value(index));

        m_d->m_plotHistoData = new Histo1DIntervalData(m_d->getCurrentHisto().get());
        m_d->m_plotHisto->setData(m_d->m_plotHistoData); // ownership goes to qwt

        m_d->m_gaussCurve->setData(new Histo1DGaussCurveData());

        m_d->m_plotRateEstimationData = new RateEstimationCurveData(m_d->getCurrentHisto().get(),
                                                                    &m_d->m_rateEstimationData);
        // ownership goes to qwt
        m_d->m_rateEstimationCurve->setData(m_d->m_plotRateEstimationData);

        m_d->m_rrSlider->setMaximum(std::log2(m_d->getCurrentHisto()->getNumberOfBins()));

        if (m_d->m_rrf == Histo1D::NoRR)
            m_d->m_rrSlider->setValue(m_d->m_rrSlider->maximum());

        //qDebug() << __PRETTY_FUNCTION__ << "new RRSlider max" << m_d->m_rrSlider->maximum();

        m_d->displayChanged();

        QSignalBlocker sb(m_d->m_histoSpin);
        m_d->m_histoSpin->setValue(index);
        emit histogramSelected(index);
    }
}

s32 Histo1DWidget::currentHistoIndex() const
{
    return m_d->m_histoIndex;
}

void Histo1DWidget::setCalibration(const std::shared_ptr<analysis::CalibrationMinMax> &calib)
{
    m_d->m_calib = calib;
    m_d->m_actionCalibUi->setVisible(m_d->m_calib != nullptr);
}

#if 0

//
// Histo1DSinkWidget
//

Histo1DSinkWidget::Histo1DSinkWidget(const Histo1DSinkPtr &sink, QWidget *parent)
    : PlotWidget(parent)
    , m_sink(sink)
{
}

int Histo1DSinkWidget::selectedHistogramIndex() const
{
    if (auto histoSpin = findChild<QSpinBox *>("histoSpin"))
        return histoSpin->value();
    return -1;
}

void Histo1DSinkWidget::selectHistogram(int histoIndex)
{
}

Histo1DSinkWidget *make_h1dsink_widget(
    const Histo1DSinkPtr &histoSink, QWidget *parent)
{
    auto plotWidget = new Histo1DSinkWidget(histoSink, parent);

    // histogram plot item
    {
        auto plotHisto = new QwtPlotHistogram;
        plotHisto->setStyle(QwtPlotHistogram::Outline);
        plotHisto->attach(plotWidget->getPlot());
    }

    // zoomer
    {
        auto zoomer = new ScrollZoomer(plotWidget->getPlot()->canvas());
        zoomer->setObjectName("zoomer");
        zoomer->setEnabled(true);
    }

    // replot timer
    {
        auto replotTimer = new QTimer(plotWidget);
        replotTimer->setInterval(ReplotPeriod_ms);
        QObject::connect(replotTimer, &QTimer::timeout,
                         plotWidget, &PlotWidget::replot);
    }

    // lin/log axis scale changer
    setup_axis_scale_changer(plotWidget, QwtPlot::yLeft, "Y-Scale");

    // histo selection spinbox
    {
        auto histoSpin = new QSpinBox();
        histoSpin->setObjectName("histoSpin");
        plotWidget->getToolBar()->addWidget(make_spacer_widget());
        plotWidget->getToolBar()->addWidget(
            make_vbox_container(QSL("Histogram #"), histoSpin, 2, -2)
            .container.release());
        QObject::connect(histoSpin, qOverload<int>(&QSpinBox::valueChanged),
                         plotWidget, &Histo1DSinkWidget::selectHistogram);
    }

    return plotWidget;
}

#endif
