#include "rate_monitor_widget.h"

#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QComboBox>
#include <QMenu>
#include <QToolBar>
#include <QSpinBox>
#include <QTimer>
#include <qwt_date.h>
#include <qwt_plot.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_scale_widget.h>

#include "git_sha1.h"
#include "qt_util.h"
#include "rate_monitor_plot_widget.h"
#include "scrollzoomer.h"
#include "util/assert.h"

using a2::RateSamplerPtr;

static const s32 ReplotPeriod_ms = 1000;

struct RateMonitorWidgetPrivate
{
    RateMonitorWidget *m_q;
    QVector<RateSamplerPtr> m_samplers;
    s32 m_currentIndex = 0;
    RateMonitorWidget::SinkPtr m_sink;
    RateMonitorWidget::SinkModifiedCallback m_sinkModifiedCallback;
    QDir m_exportDirectory;

    RateMonitorPlotWidget *m_plotWidget;
    QTimer *m_replotTimer;

    QToolBar *m_toolBar;
    QComboBox *m_yScaleCombo;
    QSpinBox *m_spin_plotIndex;

    QStatusBar *m_statusBar;
    QWidget *m_infoContainer;
    QLabel *m_labelCursorInfo;
    QLabel *m_labelPlotInfo;
    QPointF m_cursorPosition;
    s32 m_labelCursorInfoMaxWidth  = 0;
    s32 m_labelCursorInfoMaxHeight = 0;

    QwtText m_waterMarkText;
    QwtPlotTextLabel *m_waterMarkLabel;

    void postConstruct();
    void selectPlot(int index);
    void updateCursorInfoLabel();
    void updatePlotInfoLabel();
    void exportPlot();
    void exportPlotToClipboard();

    RateSamplerPtr currentSampler() const { return m_samplers.value(m_currentIndex); }
};

void RateMonitorWidgetPrivate::postConstruct()
{
    m_spin_plotIndex->setMinimum(0);
    m_spin_plotIndex->setMaximum(std::max(m_samplers.size() - 1, 0));
    m_spin_plotIndex->setVisible(m_samplers.size() > 0);
    selectPlot(0);
}

void RateMonitorWidgetPrivate::selectPlot(int index)
{
    assert(index < m_samplers.size());
    auto sampler = m_samplers.value(index);

    if (sampler)
    {
        QString xTitle = m_sink ? m_sink->objectName() + "." : QSL("");
        xTitle += QString::number(index);

        m_plotWidget->removeRateSampler(0);
        m_plotWidget->addRateSampler(sampler, xTitle);
        m_plotWidget->getPlot()->axisWidget(QwtPlot::xBottom)->setTitle(xTitle);

        QString yTitle = QSL("Rate");
        if (m_sink && !m_sink->getUnitLabel().isEmpty())
        {
            yTitle = m_sink->getUnitLabel();
        }
        m_plotWidget->getPlot()->axisWidget(QwtPlot::yLeft)->setTitle(yTitle);

        qDebug() << __PRETTY_FUNCTION__ << "added rateSampler =" << sampler.get() << ", xTitle =" << xTitle;
    }

    m_currentIndex = index;
    m_q->replot();
}

void RateMonitorWidgetPrivate::updateCursorInfoLabel()
{
    double plotX = m_cursorPosition.x();
    double plotY = m_cursorPosition.y();

    QString text;

    if (!std::isnan(plotX) && !std::isnan(plotY))
    {
        const auto &sampler = currentSampler();

        ssize_t iy = sampler->getSampleIndex(plotX / 1000.0);
        double y   = make_quiet_nan();

        if (0 <= iy && iy < static_cast<ssize_t>(sampler->historySize()))
        {
            y = sampler->getSample(iy);
        }

        /* To format the x-axis time value the plots axis scale draw is used.
         * This ensure the same formatting on the axis scale and the info
         * label. */
        auto scaleDraw  = m_plotWidget->getPlot()->axisScaleDraw(QwtPlot::xBottom);
        QString xString = scaleDraw->label(plotX).text();
        QString yUnit   = m_sink ? m_sink->getUnitLabel() : QSL("");

        text = (QString("x=%1\n"
                        "y=%2 %3\n"
                        "sampleIndex=%4"
                       )
                .arg(xString)
                .arg(y)
                .arg(yUnit)
                .arg(iy)
               );
    }

    // update the label which will calculate a new width
    m_labelCursorInfo->setText(text);

    // use the largest width and height the label ever had to stop the label from constantly changing its width
    if (m_labelCursorInfo->isVisible())
    {
        m_labelCursorInfoMaxWidth = std::max(m_labelCursorInfoMaxWidth, m_labelCursorInfo->width());
        m_labelCursorInfo->setMinimumWidth(m_labelCursorInfoMaxWidth);

        m_labelCursorInfo->setMinimumHeight(m_labelCursorInfoMaxHeight);
        m_labelCursorInfoMaxHeight = std::max(m_labelCursorInfoMaxHeight, m_labelCursorInfo->height());
    }
}

void RateMonitorWidgetPrivate::updatePlotInfoLabel()
{
    const auto &sampler = currentSampler();

    auto infoText = (QString(
            "Capacity:  %1\n"
            "Size:      %2\n"
            "# Samples: %3\n"
            "Interval:  %4"
            )
        .arg(sampler->historyCapacity())
        .arg(sampler->historySize())
        .arg(sampler->totalSamples)
        .arg(sampler->interval)
        );

    m_labelPlotInfo->setText(infoText);
}

void RateMonitorWidgetPrivate::exportPlot()
{
    auto &sink(m_sink);

    if (!sink) return;

    QString fileName = sink->objectName();
    fileName.replace("/", "_");
    fileName.replace("\\", "_");
    fileName += QSL(".pdf");

    fileName = m_exportDirectory.filePath(fileName);

    QwtPlot *plot = m_plotWidget->getPlot();

    // temporarily show title, footer and watermark on the plot
    plot->setTitle(sink->objectName());

#if 0
    // TODO: figure out what this contains for histograms and clone that functionality here
    QString footerString = m_histo->getFooter();
    QwtText footerText(footerString);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_d->m_plot->setFooter(footerText);
#endif
    m_waterMarkLabel->show();

    // do the export
    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.exportTo(plot, fileName);

    // clear title and footer, hide watermark
    plot->setTitle(QString());
    plot->setFooter(QString());
    m_waterMarkLabel->hide();
}

void RateMonitorWidgetPrivate::exportPlotToClipboard()
{
    auto &sink(m_sink);

    if (!sink) return;

    QString fileName = sink->objectName();
    fileName.replace("/", "_");
    fileName.replace("\\", "_");
    fileName += QSL(".pdf");

    fileName = m_exportDirectory.filePath(fileName);

    QwtPlot *plot = m_plotWidget->getPlot();

    // temporarily show title, footer and watermark on the plot
    plot->setTitle(sink->objectName());

#if 0
    // TODO: figure out what this contains for histograms and clone that functionality here
    QString footerString = m_histo->getFooter();
    QwtText footerText(footerString);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_d->m_plot->setFooter(footerText);
#endif
    m_waterMarkLabel->show();

    QSize size(1024, 768);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(0);

    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.renderTo(plot, image);

    plot->setTitle(QString());
    plot->setFooter(QString());
    m_waterMarkLabel->hide();

    QApplication::clipboard()->setImage(image);
}

RateMonitorWidget::RateMonitorWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorWidgetPrivate>())
{
    resize(600, 400);
    setWindowTitle(QSL("Rate Monitor"));

    m_d->m_q = this;
    m_d->m_plotWidget = new RateMonitorPlotWidget;
    m_d->m_plotWidget->setInternalLegendVisible(false);

    m_d->m_replotTimer = new QTimer(this);

    // Toolbar and actions
    m_d->m_toolBar = new QToolBar();
    auto tb = m_d->m_toolBar;
    {
        tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tb->setIconSize(QSize(16, 16));
        set_widget_font_pointsize(tb, 7);
    }

    // Y-Scale Selection
    {
        m_d->m_yScaleCombo = new QComboBox;
        auto yScaleCombo = m_d->m_yScaleCombo;

        yScaleCombo->addItem(QSL("Lin"), static_cast<int>(AxisScale::Linear));
        yScaleCombo->addItem(QSL("Log"), static_cast<int>(AxisScale::Logarithmic));

        connect(yScaleCombo, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                this, [this]() {

            auto scaling = static_cast<AxisScale>(m_d->m_yScaleCombo->currentData().toInt());
            m_d->m_plotWidget->setYAxisScale(scaling);
        });

        tb->addWidget(make_vbox_container(QSL("Y-Scale"), yScaleCombo));
    }

    tb->addAction(QIcon(":/clear_histos.png"), QSL("Clear"), this, [this]() {
        if (auto sampler = m_d->m_samplers.value(m_d->m_currentIndex))
        {
            sampler->clearHistory(true);
            replot();
        }
    });

    QAction *action = nullptr;

    // export
    {
        auto menu = new QMenu(this);
        menu->addAction(QSL("to file"), this, [this] { m_d->exportPlot(); });
        menu->addAction(QSL("to clipboard"), this, [this] { m_d->exportPlotToClipboard(); });

        auto button = make_toolbutton(QSL(":/document-pdf.png"), QSL("Export"));
        button->setStatusTip(QSL("Export plot to a PDF or image file"));
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);

        tb->addWidget(button);
    }

    // Info button
    action = tb->addAction(QIcon(":/info.png"), QSL("Info"));
    action->setCheckable(true);

    connect(action, &QAction::toggled, this, [this](bool b) {
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

    // Plot selection spinbox
    {
        auto spin_plotIndex = new QSpinBox;
        m_d->m_spin_plotIndex = spin_plotIndex;

        connect(spin_plotIndex, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this, [this] (int index) { m_d->selectPlot(index); });

        tb->addWidget(make_spacer_widget());
        tb->addWidget(make_vbox_container(QSL("Rate #"), spin_plotIndex));
    }

    // Statusbar and info widgets
    m_d->m_statusBar = make_statusbar();
    m_d->m_labelCursorInfo = new QLabel;
    m_d->m_labelPlotInfo = new QLabel;

    for (auto label: { m_d->m_labelCursorInfo, m_d->m_labelPlotInfo})
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
        layout->addWidget(m_d->m_labelPlotInfo);

        for (auto childWidget: m_d->m_infoContainer->findChildren<QWidget *>())
        {
            childWidget->setVisible(false);
        }

        m_d->m_statusBar->addPermanentWidget(m_d->m_infoContainer);
    }

    // Watermark text when exporting
    {
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
        m_d->m_waterMarkLabel->attach(m_d->m_plotWidget->getPlot());
        m_d->m_waterMarkLabel->hide();
    }

    // Reacting to zoomer changes
    // NOTE: Using the c++11 pointer-to-member syntax for the connections does
    // not work with qwt signals for some reason.
    auto zoomer = m_d->m_plotWidget->getZoomer();
    TRY_ASSERT(connect(zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));
    TRY_ASSERT(connect(zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &RateMonitorWidget::mouseCursorMovedToPlotCoord));
    TRY_ASSERT(connect(zoomer, &ScrollZoomer::mouseCursorLeftPlot,
                       this, &RateMonitorWidget::mouseCursorLeftPlot));

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
    mainLayout->addWidget(m_d->m_plotWidget);
    mainLayout->addWidget(m_d->m_statusBar);
    mainLayout->setStretch(1, 1);

    // periodic replotting
    connect(m_d->m_replotTimer, &QTimer::timeout, this, &RateMonitorWidget::replot);
    m_d->m_replotTimer->start(ReplotPeriod_ms);
}

RateMonitorWidget::RateMonitorWidget(const a2::RateSamplerPtr &sampler, QWidget *parent)
    : RateMonitorWidget(parent)
{
    assert(sampler);
    m_d->m_samplers.push_back(sampler);

    m_d->postConstruct();
}

RateMonitorWidget::RateMonitorWidget(const QVector<a2::RateSamplerPtr> &samplers, QWidget *parent)
    : RateMonitorWidget(parent)
{
    assert(samplers.size());
    m_d->m_samplers = samplers;

    m_d->postConstruct();
}

RateMonitorWidget::~RateMonitorWidget()
{
}

void RateMonitorWidget::setSink(const SinkPtr &sink, SinkModifiedCallback sinkModifiedCallback)
{
    m_d->m_sink = sink;
    m_d->m_sinkModifiedCallback = sinkModifiedCallback;
    // Select the current plot again to update the plot title
    m_d->selectPlot(m_d->m_currentIndex);
}

void RateMonitorWidget::setPlotExportDirectory(const QDir &dir)
{
    m_d->m_exportDirectory = dir;
}

void RateMonitorWidget::replot()
{
    if (m_d->m_sink)
    {
        setWindowTitle(QString("Rate %1").arg(m_d->m_sink->objectName()));
    }

    m_d->m_plotWidget->replot();
    m_d->updatePlotInfoLabel();
}

void RateMonitorWidget::zoomerZoomed(const QRectF &)
{
    replot();
}

void RateMonitorWidget::mouseCursorMovedToPlotCoord(QPointF pos)
{
    m_d->m_cursorPosition = pos;
    m_d->updateCursorInfoLabel();
}

void RateMonitorWidget::mouseCursorLeftPlot()
{
    m_d->m_cursorPosition = QPointF(make_quiet_nan(), make_quiet_nan());
    m_d->updateCursorInfoLabel();
}

void RateMonitorWidget::updateStatistics()
{
}

void RateMonitorWidget::yAxisScalingChanged()
{
}
