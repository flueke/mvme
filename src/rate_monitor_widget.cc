#include "rate_monitor_widget.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QToolBar>
#include <QSpinBox>
#include <QTimer>
#include <qwt_plot.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_scale_widget.h>

#include "git_sha1.h"
#include "qt_util.h"
#include "rate_monitor_plot_widget.h"

static const s32 ReplotPeriod_ms = 1000;

struct RateMonitorWidgetPrivate
{
    RateMonitorWidget *m_q;
    QVector<a2::RateSamplerPtr> m_samplers;
    a2::RateSampler *m_sampler;
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
    QLabel *m_labelRateMonitorInfo;
    QPointF m_cursorPosition;

    QwtText m_waterMarkText;
    QwtPlotTextLabel *m_waterMarkLabel;

    void postConstruct();
    void selectPlot(int index);
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

    auto samplerPtr = m_samplers.value(index);
    RateSampler *sampler = samplerPtr ? samplerPtr.get() : m_sampler;

    if (sampler)
    {
        QString rateTitle = m_sink ? m_sink->objectName() + "." : QSL("");
        rateTitle += QString::number(index);

        m_plotWidget->removeRate(0);
        m_plotWidget->addRate(sampler->rateHistory, rateTitle);
        m_plotWidget->getPlot()->axisWidget(QwtPlot::xBottom)->setTitle(rateTitle);
        assert(m_plotWidget->rateCount() == 1);

        qDebug() << __PRETTY_FUNCTION__ << "added rateHistory =" << sampler->rateHistory.get() << ", title =" << rateTitle;
    }

    m_currentIndex = index;
    m_q->replot();
}

RateMonitorWidget::RateMonitorWidget(QWidget *parent)
    : QWidget(parent)
    , m_d(std::make_unique<RateMonitorWidgetPrivate>())
{
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

    // export
    QAction *action = nullptr;
    action = tb->addAction(QIcon(":/document-pdf.png"), QSL("Export"), this, &RateMonitorWidget::exportPlot);
    action->setStatusTip(QSL("Export plot to a PDF or image file"));

    // Spinbox for cycling through samplers. Added to the toolbar.
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
    m_d->m_labelRateMonitorInfo = new QLabel;

    for (auto label: { m_d->m_labelCursorInfo, m_d->m_labelRateMonitorInfo})
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
        layout->addWidget(m_d->m_labelRateMonitorInfo);

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
    m_d->m_sampler = sampler.get();

    m_d->postConstruct();
}

RateMonitorWidget::RateMonitorWidget(a2::RateSampler *sampler, QWidget *parent)
    : RateMonitorWidget(parent)
{
    assert(sampler);
    m_d->m_sampler = sampler;

    m_d->postConstruct();
}

RateMonitorWidget::RateMonitorWidget(const QVector<a2::RateSamplerPtr> &samplers, QWidget *parent)
    : RateMonitorWidget(parent)
{
    assert(samplers.size());
    m_d->m_samplers = samplers;
    m_d->m_sampler = samplers[0].get();

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
}

void RateMonitorWidget::exportPlot()
{
    assert(m_d->m_sink);

    auto &sink(m_d->m_sink);

    QString fileName = sink->objectName();
    fileName.replace("/", "_");
    fileName.replace("\\", "_");
    fileName += QSL(".pdf");

    fileName = m_d->m_exportDirectory.filePath(fileName);

    QwtPlot *plot = m_d->m_plotWidget->getPlot();

    // temporarily show title, footer and watermark on the plot
    plot->setTitle(sink->objectName());

#if 0
    // TODO: figure out what this contains for histograms and clone that functionality here
    QString footerString = m_histo->getFooter();
    QwtText footerText(footerString);
    footerText.setRenderFlags(Qt::AlignLeft);
    m_d->m_plot->setFooter(footerText);
#endif
    m_d->m_waterMarkLabel->show();

    // do the export
    QwtPlotRenderer renderer;
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground | QwtPlotRenderer::DiscardCanvasBackground);
    renderer.setLayoutFlag(QwtPlotRenderer::FrameWithScales);
    renderer.exportTo(plot, fileName);

    // clear title and footer, hide watermark
    plot->setTitle(QString());
    plot->setFooter(QString());
    m_d->m_waterMarkLabel->hide();
}

void RateMonitorWidget::zoomerZoomed(const QRectF &)
{
}

void RateMonitorWidget::mouseCursorMovedToPlotCoord(QPointF)
{
}

void RateMonitorWidget::mouseCursorLeftPlot()
{
}

void RateMonitorWidget::updateStatistics()
{
}

void RateMonitorWidget::yAxisScalingChanged()
{
}
