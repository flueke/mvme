/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include "scrollzoomer.h"
#include "util.h"
#include "analysis/analysis.h"
#include "qt-collapsible-section/Section.h"
#include "mvme_context.h"

#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_point_data.h>
#include <qwt_scale_engine.h>
#include <qwt_scale_widget.h>
#include <qwt_text.h>

#include <QComboBox>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>

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

static const double FWHMSigmaFactor = 2.3548;

static inline double squared(double x)
{
    return x * x;
}

/* Calculates a gauss fit using the currently visible maximum histogram value.
 *
 * Note: The resolution is independent of the underlying histograms resolution.
 * Instead NumberOfPoints samples are used at all zoom levels.
 */
class Histo1DGaussCurveData: public QwtSyntheticPointData
{
    static const size_t NumberOfPoints = 1000;

    public:
        Histo1DGaussCurveData(Histo1D *histo)
            : QwtSyntheticPointData(NumberOfPoints)
            , m_histo(histo)
        {
        }

        virtual double y(double x) const override
        {
            double s = m_stats.fwhm / FWHMSigmaFactor;
            // Instead of using the center of the max bin the center point
            // between the fwhm edges is used. This makes the curve remain in a
            // much more stable x-position.
            double a = m_stats.fwhmCenter;

            double firstTerm  = m_stats.maxValue; // This is (1.0 / (SqrtPI2 * s)) if the resulting area should be 1.
            double exponent   = -0.5 * ((squared(x - a) / squared(s)));
            double secondTerm = std::exp(exponent);
            double yValue     = firstTerm * secondTerm;

            //qDebug("x=%lf, s=%lf, a=%lf, stats.maxBin=%d",
            //       x, s, a, m_stats.maxBin);
            //qDebug("firstTerm=%lf, exponent=%lf, secondTerm=%lf, yValue=%lf",
            //       firstTerm, exponent, secondTerm, yValue);

            return yValue;
        }

        void setStats(Histo1DStatistics stats)
        {
            m_stats = stats;
        }

    private:
        Histo1D *m_histo;
        Histo1DStatistics m_stats;
};

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

static const double PlotTextLayerZ  = 1000.0;
static const double PlotGaussLayerZ = 1001.0;

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

    QAction *m_actionRateEstimation,
            *m_actionSubRange,
            *m_actionChangeRes,
            *m_actionGaussFit,
            *m_actionCalibUi,
            *m_actionInfo;

    RateEstimationData m_rateEstimationData;
    QwtPlotPicker *m_ratePointPicker;
    QwtPlotMarker *m_rateX1Marker;
    QwtPlotMarker *m_rateX2Marker;
    QwtPlotMarker *m_rateFormulaMarker;

    QwtPlotCurve *m_gaussCurve = nullptr;

    QComboBox *m_yScaleCombo;

    CalibUi m_calibUi;

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
        select_by_resolution(combo_xBins, m_q->m_sink->m_bins);

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
            if (m_q->m_sink->m_bins != combo_xBins->currentData().toInt())
            {
                m_q->m_sink->m_bins = combo_xBins->currentData().toInt();
                m_q->m_context->analysisOperatorEdited(m_q->m_sink);
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

static QWidget *make_vbox_container(const QString &labelText, QWidget *widget)
{
    auto label = new QLabel(labelText);
    label->setAlignment(Qt::AlignCenter);

    auto container = new QWidget;
    auto layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(label);
    layout->addWidget(widget);

    return container;
}

Histo1DWidget::Histo1DWidget(const Histo1DPtr &histoPtr, QWidget *parent)
    : Histo1DWidget(histoPtr.get(), parent)
{
    m_histoPtr = histoPtr;
}

Histo1DWidget::Histo1DWidget(Histo1D *histo, QWidget *parent)
    : QWidget(parent)
    , m_d(new Histo1DWidgetPrivate)
    , m_histo(histo)
    , m_plotCurve(new QwtPlotCurve)
    , m_replotTimer(new QTimer(this))
    , m_cursorPosition(make_quiet_nan(), make_quiet_nan())
    , m_context(nullptr)
{
    m_d->m_q = this;

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
                this, &Histo1DWidget::displayChanged);

        tb->addWidget(make_vbox_container(QSL("Y-Scale"), yScaleCombo));
    }

    m_d->m_actionGaussFit = tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("Gauss"));
    m_d->m_actionGaussFit->setCheckable(true);
    connect(m_d->m_actionGaussFit, &QAction::toggled, this, &Histo1DWidget::on_tb_gauss_toggled);

    m_d->m_actionRateEstimation = tb->addAction(QIcon(":/generic_chart_with_pencil.png"), QSL("Rate Est."));
    m_d->m_actionRateEstimation->setStatusTip(QSL("Rate Estimation"));
    m_d->m_actionRateEstimation->setCheckable(true);
    connect(m_d->m_actionRateEstimation, &QAction::toggled, this, &Histo1DWidget::on_tb_rate_toggled);

    tb->addAction(QIcon(":/clear_histos.png"), QSL("Clear"), this, [this]() {
        m_histo->clear();
        replot();
    });

    action = tb->addAction(QIcon(":/document-pdf.png"), QSL("Export"), this, &Histo1DWidget::exportPlot);
    action->setStatusTip(QSL("Export plot to a PDF or image file"));

    action = tb->addAction(QIcon(":/document-save.png"), QSL("Save"), this, &Histo1DWidget::saveHistogram);
    action->setStatusTip(QSL("Save the histogram to a text file"));

    m_d->m_actionSubRange = tb->addAction(QIcon(":/histo_subrange.png"), QSL("Subrange"), this, &Histo1DWidget::on_tb_subRange_clicked);
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
    connect(m_d->m_actionCalibUi, &QAction::toggled, this, [this](bool b) { m_d->setCalibUiVisible(b); });

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


    tb->addWidget(make_spacer_widget());

    // Setup the plot
    m_d->m_plot = new QwtPlot;
    m_plotCurve->setStyle(QwtPlotCurve::Steps);
    m_plotCurve->setCurveAttribute(QwtPlotCurve::Inverted);
    m_plotCurve->attach(m_d->m_plot);

    m_d->m_plot->axisWidget(QwtPlot::yLeft)->setTitle("Counts");

    connect(m_replotTimer, SIGNAL(timeout()), this, SLOT(replot()));
    m_replotTimer->start(ReplotPeriod_ms);

    m_d->m_plot->canvas()->setMouseTracking(true);

    m_zoomer = new ScrollZoomer(m_d->m_plot->canvas());

    m_zoomer->setVScrollBarMode(Qt::ScrollBarAlwaysOff);
    m_zoomer->setZoomBase();
    qDebug() << "zoomRectIndex()" << m_zoomer->zoomRectIndex();

    connect(m_zoomer, &ScrollZoomer::zoomed, this, &Histo1DWidget::zoomerZoomed);
    connect(m_zoomer, &ScrollZoomer::mouseCursorMovedTo, this, &Histo1DWidget::mouseCursorMovedToPlotCoord);
    connect(m_zoomer, &ScrollZoomer::mouseCursorLeftPlot, this, &Histo1DWidget::mouseCursorLeftPlot);

    connect(m_histo, &Histo1D::axisBinningChanged, this, [this] (Qt::Axis) {
        // Handle axis changes by zooming out fully. This will make sure
        // possible axis scale changes are immediately visible and the zoomer
        // is in a clean state.
        m_zoomer->setZoomStack(QStack<QRectF>(), -1);
        m_zoomer->zoom(0);
        replot();
    });

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
    m_statsTextItem->attach(m_d->m_plot);

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
    m_d->m_calibUi.resetToFilterButton = new QPushButton(QSL("Restore"));
    m_d->m_calibUi.resetToFilterButton->setToolTip(QSL("Restore base unit values from source calibration"));
    m_d->m_calibUi.closeButton = new QPushButton(QSL("Close"));

    m_d->m_calibUi.lastFocusedActual = m_d->m_calibUi.actual2;
    m_d->m_calibUi.actual1->installEventFilter(this);
    m_d->m_calibUi.actual2->installEventFilter(this);

    connect(m_d->m_calibUi.applyButton, &QPushButton::clicked, this, &Histo1DWidget::calibApply);
    connect(m_d->m_calibUi.fillMaxButton, &QPushButton::clicked, this, &Histo1DWidget::calibFillMax);
    connect(m_d->m_calibUi.resetToFilterButton, &QPushButton::clicked, this, &Histo1DWidget::calibResetToFilter);
    connect(m_d->m_calibUi.closeButton, &QPushButton::clicked, this, [this]() { m_d->m_calibUi.window->reject(); });

    QVector<QDoubleSpinBox *> spins = { m_d->m_calibUi.actual1, m_d->m_calibUi.actual2, m_d->m_calibUi.target1, m_d->m_calibUi.target2 };

    for (auto spin: spins)
    {
        spin->setDecimals(4);
        spin->setSingleStep(0.0001);
        spin->setMinimum(std::numeric_limits<double>::lowest());
        spin->setMaximum(std::numeric_limits<double>::max());
        spin->setValue(0.0);
    }

    m_d->m_calibUi.window = new QDialog(this);
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

    calibLayout->addWidget(m_d->m_calibUi.resetToFilterButton, 4, 0, 1, 1);
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

    m_d->m_rateX1Marker = make_position_marker(m_d->m_plot);
    m_d->m_rateX2Marker = make_position_marker(m_d->m_plot);

    m_d->m_rateFormulaMarker = new QwtPlotMarker;
    m_d->m_rateFormulaMarker->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    m_d->m_rateFormulaMarker->setZ(PlotTextLayerZ);
    m_d->m_rateFormulaMarker->attach(m_d->m_plot);
    m_d->m_rateFormulaMarker->hide();

    m_d->m_ratePointPicker = new QwtPlotPicker(QwtPlot::xBottom, QwtPlot::yLeft,
                                               QwtPicker::VLineRubberBand, QwtPicker::ActiveOnly,
                                               m_d->m_plot->canvas());
    QPen pickerPen(Qt::red);
    m_d->m_ratePointPicker->setTrackerPen(pickerPen);
    m_d->m_ratePointPicker->setRubberBandPen(pickerPen);
    m_d->m_ratePointPicker->setStateMachine(new AutoBeginClickPointMachine);
    m_d->m_ratePointPicker->setEnabled(false);

    connect(m_d->m_ratePointPicker, static_cast<void (QwtPlotPicker::*)(const QPointF &)>(&QwtPlotPicker::selected), [this](const QPointF &pos) {

        if (std::isnan(m_d->m_rateEstimationData.x1))
        {
            m_d->m_rateEstimationData.x1 = pos.x();

            m_d->m_rateX1Marker->setXValue(m_d->m_rateEstimationData.x1);
            m_d->m_rateX1Marker->setLabel(QString("    x1=%1").arg(m_d->m_rateEstimationData.x1));
            m_d->m_rateX1Marker->show();
        }
        else if (std::isnan(m_d->m_rateEstimationData.x2))
        {
            m_d->m_rateEstimationData.x2 = pos.x();

            if (m_d->m_rateEstimationData.x1 > m_d->m_rateEstimationData.x2)
            {
                std::swap(m_d->m_rateEstimationData.x1, m_d->m_rateEstimationData.x2);
            }

            m_d->m_rateEstimationData.visible = true;
            m_d->m_ratePointPicker->setEnabled(false);
            m_zoomer->setEnabled(true);

            // set both x1 and x2 as they might have been swapped above
            m_d->m_rateX1Marker->setXValue(m_d->m_rateEstimationData.x1);
            m_d->m_rateX1Marker->setLabel(QString("    x1=%1").arg(m_d->m_rateEstimationData.x1));
            m_d->m_rateX2Marker->setXValue(m_d->m_rateEstimationData.x2);
            m_d->m_rateX2Marker->setLabel(QString("    x2=%1").arg(m_d->m_rateEstimationData.x2));
            m_d->m_rateX2Marker->show();
        }
        else
        {
            InvalidCodePath;
        }

        replot();
    });

    //
    // Gauss Curve
    //
    m_d->m_gaussCurve = new QwtPlotCurve;
    m_d->m_gaussCurve->setZ(PlotGaussLayerZ);
    m_d->m_gaussCurve->setPen(Qt::green, 2.0);
    m_d->m_gaussCurve->attach(m_d->m_plot);
    m_d->m_gaussCurve->hide();

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

    setHistogram(histo);
}

Histo1DWidget::~Histo1DWidget()
{
    delete m_plotCurve;
    delete m_statsText;
    delete m_d;
}

void Histo1DWidget::setHistogram(const Histo1DPtr &histoPtr)
{
    m_histoPtr = histoPtr;

    setHistogram(histoPtr.get());
}

void Histo1DWidget::setHistogram(Histo1D *histo)
{
    m_histo = histo;
    m_plotCurve->setData(new Histo1DPointData(m_histo));
    m_d->m_gaussCurve->setData(new Histo1DGaussCurveData(m_histo));

    // Reset the zoom stack and zoom fully zoom out as the scales might be
    // completely different now.
    // FIXME: this is not good for the usage of projection widgets where the
    // histo is replaced with a similar one. The zoom level should stay the same in that case...
    // Maybe compare the axses before replacing the histo and decide based on
    // that whether to reset the zoom stack or not.
    //m_zoomer->setZoomStack(QStack<QRectF>(), -1);
    //m_zoomer->zoom(0);

    displayChanged();
    replot();
}

void Histo1DWidget::updateAxisScales()
{
    // Scale the y axis using the currently visible max value plus 20%
    double maxValue = m_stats.maxValue;

    // force a minimum of 10 units in y
    if (maxValue <= 1.0)
        maxValue = 10.0;

    double base;

    if (yAxisIsLog())
    {
        base = 1.0;
        maxValue = std::pow(maxValue, 1.2);
    }
    else
    {
        base = 0.0;
        maxValue = maxValue * 1.2;
    }

    // This sets a fixed y axis scale effectively overriding any changes made
    // by the scrollzoomer.
    m_d->m_plot->setAxisScale(QwtPlot::yLeft, base, maxValue);

    // xAxis
    if (m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        m_d->m_plot->setAxisScale(QwtPlot::xBottom, m_histo->getXMin(), m_histo->getXMax());
        m_zoomer->setZoomBase();
    }

    m_d->m_plot->updateAxes();
}

void Histo1DWidget::replot()
{
    updateStatistics();
    updateAxisScales();
    updateCursorInfoLabel();

    // update histo info label
    auto infoText = QString("Underflow: %1\n"
                            "Overflow:  %2")
        .arg(m_histo->getUnderflow())
        .arg(m_histo->getOverflow());


    // rate and efficiency estimation
    if (m_d->m_rateEstimationData.visible)
    {
        /* This code tries to interpolate the exponential function formed by
         * the two selected data points. */
        double x1 = m_d->m_rateEstimationData.x1;
        double x2 = m_d->m_rateEstimationData.x2;
        double y1 = m_histo->getValue(x1);
        double y2 = m_histo->getValue(x2);

        double tau = (x2 - x1) / log(y1 / y2);
        double e = exp(1.0);
        double c = pow(e, x1 / tau) * y1;
        double c_norm = c / m_histo->getBinWidth(); // norm to x-axis scale
        double freeRate = 1.0 / tau; // 1/x-axis unit
        double freeCounts = c_norm * tau * (1 - pow(e, -(x2 / tau))); // for interval 0..x2
        double histoCounts = m_histo->calcStatistics(0.0, x2).entryCount;
        double efficiency  = histoCounts / freeCounts;

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

        if (!std::isnan(c) && !std::isnan(tau) && !std::isnan(efficiency))
        {
            markerText = QString(QSL("freeRate=%1 <sup>1</sup>&frasl;<sub>%2</sub>; eff=%3")
                                 .arg(freeRate, 0, 'g', 4)
                                 .arg(m_histo->getAxisInfo(Qt::XAxis).unit)
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
        m_d->m_rateFormulaMarker->setXValue(x1);

        /* The goal is to draw the marker at 0.9 of the plot height. Doing this
         * in plot coordinates does work for a linear y-axis scale but
         * positions the text way too high for a logarithmic scale. Instead of
         * using plot coordinates directly we're using 0.9 of the canvas height
         * and transform that pixel coordinate to a plot coordinate.
         */
        s32 canvasHeight = m_d->m_plot->canvas()->height();
        s32 pixelY = canvasHeight - canvasHeight * 0.9;
        double plotY = m_d->m_plot->canvasMap(QwtPlot::yLeft).invTransform(pixelY);

        m_d->m_rateFormulaMarker->setYValue(plotY);
        m_d->m_rateFormulaMarker->setLabel(rateFormulaText);
        m_d->m_rateFormulaMarker->show();
    }

    m_d->m_labelHistoInfo->setText(infoText);

    // window and axis titles
    auto name = m_histo->objectName();
    setWindowTitle(QString("Histogram %1").arg(name));

    auto axisInfo = m_histo->getAxisInfo(Qt::XAxis);
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

void Histo1DWidget::displayChanged()
{
    auto scaleType = static_cast<AxisScaleType>(m_d->m_yScaleCombo->currentData().toInt());

    if (scaleType == AxisScaleType::Linear && !yAxisIsLin())
    {
        m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, new QwtLinearScaleEngine);
        m_d->m_plot->setAxisAutoScale(QwtPlot::yLeft, true);
    }
    else if (scaleType == AxisScaleType::Logarithmic && !yAxisIsLog())
    {
        auto scaleEngine = new QwtLogScaleEngine;
        scaleEngine->setTransformation(new MinBoundLogTransform);
        m_d->m_plot->setAxisScaleEngine(QwtPlot::yLeft, scaleEngine);
    }

    replot();
}

void Histo1DWidget::zoomerZoomed(const QRectF &zoomRect)
{
    if (m_zoomer->zoomRectIndex() == 0)
    {
        // fully zoomed out -> set to full resolution
        m_d->m_plot->setAxisScale(QwtPlot::xBottom, m_histo->getXMin(), m_histo->getXMax());
        m_d->m_plot->replot();
        m_zoomer->setZoomBase();
    }

    // do not zoom outside the histogram range
    auto scaleDiv = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom);
    double lowerBound = scaleDiv.lowerBound();
    double upperBound = scaleDiv.upperBound();

    if (lowerBound <= upperBound)
    {
        if (lowerBound < m_histo->getXMin())
        {
            scaleDiv.setLowerBound(m_histo->getXMin());
        }

        if (upperBound > m_histo->getXMax())
        {
            scaleDiv.setUpperBound(m_histo->getXMax());
        }
    }
    else
    {
        if (lowerBound > m_histo->getXMin())
        {
            scaleDiv.setLowerBound(m_histo->getXMin());
        }

        if (upperBound < m_histo->getXMax())
        {
            scaleDiv.setUpperBound(m_histo->getXMax());
        }
    }

    m_d->m_plot->setAxisScaleDiv(QwtPlot::xBottom, scaleDiv);

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
    double lowerBound = qFloor(m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound());
    double upperBound = qCeil(m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound());

    m_stats = m_histo->calcStatistics(lowerBound, upperBound);

    static const QString textTemplate = QSL(
        "<table>"
        "<tr><td align=\"left\">RMS    </td><td>%L1</td></tr>"
        "<tr><td align=\"left\">FWHM   </td><td>%L2</td></tr>"
        "<tr><td align=\"left\">Mean   </td><td>%L3</td></tr>"
        "<tr><td align=\"left\">Max    </td><td>%L4</td></tr>"
        "<tr><td align=\"left\">Max Y  </td><td>%L5</td></tr>"
        "<tr><td align=\"left\">Counts </td><td>%L6</td></tr>"
        "</table>"
        );

    double maxBinCenter = (m_stats.entryCount > 0) ? m_histo->getBinCenter(m_stats.maxBin) : 0.0;

    static const int fieldWidth = 0;
    QString buffer = textTemplate
        .arg(m_stats.sigma, fieldWidth)
        .arg(m_stats.fwhm)
        .arg(m_stats.mean, fieldWidth)
        .arg(maxBinCenter, fieldWidth)
        .arg(m_stats.maxValue, fieldWidth)
        .arg(m_stats.entryCount, fieldWidth)
        ;

    m_statsText->setText(buffer, QwtText::RichText);
    m_statsTextItem->setText(*m_statsText);

    auto curveData = reinterpret_cast<Histo1DGaussCurveData *>(m_d->m_gaussCurve->data());
    curveData->setStats(m_stats);
}

bool Histo1DWidget::yAxisIsLog()
{
    return dynamic_cast<QwtLogScaleEngine *>(m_d->m_plot->axisScaleEngine(QwtPlot::yLeft));
}

bool Histo1DWidget::yAxisIsLin()
{
    return dynamic_cast<QwtLinearScaleEngine *>(m_d->m_plot->axisScaleEngine(QwtPlot::yLeft));
}

void Histo1DWidget::exportPlot()
{
    QString fileName = m_histo->objectName();
    fileName.replace("/", "_");
    fileName.replace("\\", "_");
    fileName += QSL(".pdf");

    if (m_context)
    {
        fileName = QDir(m_context->getWorkspacePath(QSL("PlotsDirectory"))).filePath(fileName);
    }

    m_d->m_plot->setTitle(m_histo->getTitle());
    QwtText footerText(m_histo->getFooter());
    footerText.setRenderFlags(Qt::AlignLeft);
    m_d->m_plot->setFooter(footerText);

    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.exportTo(m_d->m_plot, fileName);

    m_d->m_plot->setTitle(QString());
    m_d->m_plot->setFooter(QString());
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
        double binLowEdge = binning.getBinLowEdge((u32)binX);

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
    m_d->m_labelCursorInfo->setText(text);

    // use the largest width and height the label ever had to stop the label from constantly changing its width
    if (m_d->m_labelCursorInfo->isVisible())
    {
        m_d->m_labelCursorInfoMaxWidth = std::max(m_d->m_labelCursorInfoMaxWidth, m_d->m_labelCursorInfo->width());
        m_d->m_labelCursorInfo->setMinimumWidth(m_d->m_labelCursorInfoMaxWidth);

        m_d->m_labelCursorInfo->setMinimumHeight(m_d->m_labelCursorInfoMaxHeight);
        m_d->m_labelCursorInfoMaxHeight = std::max(m_d->m_labelCursorInfoMaxHeight, m_d->m_labelCursorInfo->height());
    }
}

void Histo1DWidget::setCalibrationInfo(const std::shared_ptr<analysis::CalibrationMinMax> &calib, s32 histoAddress)
{
    m_calib = calib;
    m_histoAddress = histoAddress;
    m_d->m_actionCalibUi->setVisible(m_calib != nullptr);
}

void Histo1DWidget::calibApply()
{
    Q_ASSERT(m_calib);
    Q_ASSERT(m_context);

    double a1 = m_d->m_calibUi.actual1->value();
    double a2 = m_d->m_calibUi.actual2->value();
    double t1 = m_d->m_calibUi.target1->value();
    double t2 = m_d->m_calibUi.target2->value();

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

    m_d->m_calibUi.actual1->setValue(m_d->m_calibUi.target1->value());
    m_d->m_calibUi.actual2->setValue(m_d->m_calibUi.target2->value());

    AnalysisPauser pauser(m_context);
    m_calib->setCalibration(address, targetMin, targetMax);
    analysis::do_beginRun_forward(m_calib.get());

    on_tb_rate_toggled(m_d->m_rateEstimationData.visible);
}

void Histo1DWidget::calibResetToFilter()
{
    Q_ASSERT(m_calib);
    Q_ASSERT(m_context);

    using namespace analysis;

    Pipe *inputPipe = m_calib->getSlot(0)->inputPipe;
    if (inputPipe)
    {
        Parameter *inputParam = inputPipe->getParameter(m_histoAddress);
        if (inputParam)
        {
            double minValue = inputParam->lowerLimit;
            double maxValue = inputParam->upperLimit;
            AnalysisPauser pauser(m_context);
            m_calib->setCalibration(m_histoAddress, minValue, maxValue);
            analysis::do_beginRun_forward(m_calib.get());
        }
    }
}

void Histo1DWidget::calibFillMax()
{
    double maxAt = m_histo->getAxisBinning(Qt::XAxis).getBinLowEdge(m_stats.maxBin);
    m_d->m_calibUi.lastFocusedActual->setValue(maxAt);
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

    return QWidget::event(e);
}

void Histo1DWidget::setSink(const SinkPtr &sink, HistoSinkCallback sinkModifiedCallback)
{
    Q_ASSERT(sink);
    m_sink = sink;
    m_sinkModifiedCallback = sinkModifiedCallback;
    m_d->m_actionSubRange->setEnabled(true);
    m_d->m_actionChangeRes->setEnabled(true);
}

void Histo1DWidget::on_tb_subRange_clicked()
{
    Q_ASSERT(m_sink);
    double visibleMinX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = m_d->m_plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    Histo1DSubRangeDialog dialog(m_sink, m_sinkModifiedCallback, visibleMinX, visibleMaxX, this);
    dialog.exec();
}

void Histo1DWidget::on_tb_rate_toggled(bool checked)
{
    if (checked)
    {
        m_d->m_rateEstimationData = RateEstimationData();
        m_d->m_ratePointPicker->setEnabled(true);
        m_zoomer->setEnabled(false);
    }
    else
    {
        m_d->m_rateEstimationData.visible = false;
        m_d->m_ratePointPicker->setEnabled(false);
        m_zoomer->setEnabled(true);
        m_d->m_rateX1Marker->hide();
        m_d->m_rateX2Marker->hide();
        m_d->m_rateFormulaMarker->hide();
        replot();
    }
}

void Histo1DWidget::on_tb_gauss_toggled(bool checked)
{
    if (checked)
    {
        m_d->m_gaussCurve->show();
    }
    else
    {
        m_d->m_gaussCurve->hide();
    }

    replot();
}

void Histo1DWidget::on_tb_test_clicked()
{
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

    /* Create a spinbox to cycle through the histograms and add it to the
     * Histo1DWidgets toolbar. */
    auto histoSpinBox = new QSpinBox;
    histoSpinBox->setMaximum(histos.size() - 1);
    connect(histoSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            this, &Histo1DListWidget::onHistoSpinBoxValueChanged);

    m_histoWidget->m_d->m_toolBar->addWidget(make_vbox_container(QSL("Histogram #"), histoSpinBox));

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
    if (auto histo = m_histos.value(index))
    {
        m_histoWidget->setHistogram(histo.get());
        m_histoWidget->setContext(m_context);

        if (m_calib)
        {
            m_histoWidget->setCalibrationInfo(m_calib, index);
        }

        if (m_sink)
        {
            m_histoWidget->setSink(m_sink, m_sinkModifiedCallback);
        }
    }
}

void Histo1DListWidget::setCalibration(const std::shared_ptr<analysis::CalibrationMinMax> &calib)
{
    m_calib = calib;
    if (m_calib)
    {
        m_histoWidget->setCalibrationInfo(m_calib, m_currentIndex);
    }
}

void Histo1DListWidget::setSink(const SinkPtr &sink, HistoSinkCallback sinkModifiedCallback)
{
    Q_ASSERT(sink);
    m_sink = sink;
    m_sinkModifiedCallback = sinkModifiedCallback;

    onHistoSpinBoxValueChanged(m_currentIndex);
}
