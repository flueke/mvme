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
#include "rate_monitor_widget.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QMenu>
#include <QToolBar>
#include <QSpinBox>
#include <QTimer>
#include <qwt_date.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_renderer.h>
#include <qwt_plot_textlabel.h>
#include <qwt_scale_widget.h>

#include "git_sha1.h"
#include "mvme_session.h"
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
    QCheckBox *m_cb_combinedView;
    QSpinBox *m_spin_plotIndex;

    QStatusBar *m_statusBar;
    QWidget *m_infoContainer;
    QLabel *m_labelCursorInfo,
           *m_labelPlotInfo,
           *m_labelVisibleRangeInfo;

    NonShrinkingLabelHelper m_labelCursorInfoHelper,
                            m_labelVisibleRangeInfoHelper;

    QPointF m_cursorPosition;

    QwtText m_waterMarkText;
    QwtPlotTextLabel *m_waterMarkLabel;

    void selectPlot(int index);
    void updateVisibleRangeInfoLabel();
    void updateCursorInfoLabel();
    void updatePlotInfoLabel();
    void exportPlot();
    void exportPlotToClipboard();

    RateSamplerPtr currentSampler() const { return m_samplers.value(m_currentIndex); }
};

/* Constructs a name for the sampler with the given samplerIndex by looking at the
 * samplers input source. */
QString make_ratemonitor_plot_title(const RateMonitorWidget::SinkPtr &sink, s32 samplerIndex)
{
    QString result(QSL("not set"));

    if (sink)
    {
        s32 inputIndex = sink->getSamplerToInputMapping().value(samplerIndex, -1);
        auto slot = sink->getSlot(inputIndex);

        if (slot && slot->inputPipe && slot->inputPipe->getSource())
        {
            auto src = slot->inputPipe->getSource();

            s32 relativeSamplerIndex = samplerIndex - sink->getSamplerStartOffset(inputIndex);

            s32 titleIndexValue = (slot->paramIndex == analysis::Slot::NoParamIndex
                                   ? relativeSamplerIndex
                                   : slot->paramIndex);

            if (slot->inputPipe->getSize() == 1)
            {
                result = src->objectName();
            }
            else
            {
                result = src->objectName() + "." + QString::number(titleIndexValue);
            }
        }
    }

    return result;
}

const QVector<QColor> make_plot_colors()
{
    static const QVector<QColor> result =
    {
        "#000000",
        "#e6194b",
        "#3cb44b",
        "#ffe119",
        "#0082c8",
        "#f58231",
        "#911eb4",
        "#46f0f0",
        "#f032e6",
        "#d2f53c",
        "#fabebe",
        "#008080",
        "#e6beff",
        "#aa6e28",
        "#fffac8",
        "#800000",
        "#aaffc3",
        "#808000",
        "#ffd8b1",
        "#000080",
    };

    return result;
};

/* Select the plot with the given index or switch to combined view if index is negative.
 */
void RateMonitorWidgetPrivate::selectPlot(int index)
{
    assert(index < m_samplers.size());

    // Combined view showing all sampler curves in the same plot.
    static const auto colors = make_plot_colors();
    const int ncolors = colors.size();
    auto sampler = m_samplers.value(index);

    QString yTitle = QSL("Rate");

    if (0 <= index)
    {
        // Single curve display.

        if (sampler)
        {
            m_plotWidget->removeAllRateSamplers();

            QString plotTitle = make_ratemonitor_plot_title(m_sink, index);
            auto color = colors.value(index % ncolors);
            m_plotWidget->addRateSampler(sampler, plotTitle, color);
            m_plotWidget->getPlot()->axisWidget(QwtPlot::xBottom)->setTitle(plotTitle);

            //qDebug() << __PRETTY_FUNCTION__ << "added rateSampler =" << sampler.get()
            //    << ", plotTitle =" << plotTitle;
        }

        m_plotWidget->getPlot()->insertLegend(nullptr);
    }
    else // negative index -> show combined view
    {
        m_plotWidget->removeAllRateSamplers();

        for (s32 samplerIndex = 0; samplerIndex < m_samplers.size(); samplerIndex++)
        {
            QString plotTitle = make_ratemonitor_plot_title(m_sink, samplerIndex);

            auto color = colors.value(samplerIndex % ncolors);

            m_plotWidget->addRateSampler(m_samplers[samplerIndex], plotTitle, color);

            //qDebug() << __PRETTY_FUNCTION__ << "added rateSampler =" << sampler.get()
            //    << ", plotTitle =" << plotTitle;
        }

        auto legend = std::make_unique<QwtLegend>();
        //legend->setDefaultItemMode(QwtLegendData::Checkable);
        m_plotWidget->getPlot()->insertLegend(legend.release(), QwtPlot::LeftLegend);

        m_plotWidget->getPlot()->axisWidget(QwtPlot::xBottom)->setTitle(QString());
    }

    if (m_sink && !m_sink->getUnitLabel().isEmpty())
    {
        yTitle = m_sink->getUnitLabel();
    }

    m_plotWidget->getPlot()->axisWidget(QwtPlot::yLeft)->setTitle(yTitle);
    m_currentIndex = index;
    m_q->replot();
}

void RateMonitorWidgetPrivate::updateVisibleRangeInfoLabel()
{
    auto plot = m_plotWidget->getPlot();
    double visibleMinX = plot->axisScaleDiv(QwtPlot::xBottom).lowerBound();
    double visibleMaxX = plot->axisScaleDiv(QwtPlot::xBottom).upperBound();
    auto scaleDraw     = plot->axisScaleDraw(QwtPlot::xBottom);
    QString minStr     = scaleDraw->label(visibleMinX).text();
    QString maxStr     = scaleDraw->label(visibleMaxX).text();
    QString text;

    if (const auto sampler = currentSampler())
    {
        ssize_t minIdx = sampler->getSampleIndex(visibleMinX / 1000.0);
        ssize_t maxIdx = sampler->getSampleIndex(visibleMaxX / 1000.0);

        double  avg  = 0.0;
        ssize_t size = sampler->rateHistory.size();

        if (0 <= minIdx && minIdx < size
            && 0 <= maxIdx && maxIdx < size)
        {
            double sum   = std::accumulate(sampler->rateHistory.begin() + minIdx,
                                           sampler->rateHistory.begin() + maxIdx + 1,
                                           0.0);
            double count = maxIdx - minIdx + 1;
            avg = sum / count;
        }

        text = (QSL("Visible Interval:\n"
                    "xMin = %1\n"
                    "xMax = %2\n"
                    "avg. Rate = %3"
                   )
                .arg(minStr)
                .arg(maxStr)
                .arg(avg)
               );
    }
    else
    {
        text = (QSL("Visible Interval:\n"
                    "xMin = %1\n"
                    "xMax = %2"
                   )
                .arg(minStr)
                .arg(maxStr)
               );
    }

    m_labelVisibleRangeInfoHelper.setText(text);
}

void RateMonitorWidgetPrivate::updateCursorInfoLabel()
{
    double plotX = m_cursorPosition.x();
    double plotY = m_cursorPosition.y();

    QString text;

    if (!std::isnan(plotX) && !std::isnan(plotY))
    {
        auto sampler = currentSampler() ? currentSampler() : m_samplers.value(0);

        if (sampler)
        {
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
    }

    m_labelCursorInfoHelper.setText(text);
}

void RateMonitorWidgetPrivate::updatePlotInfoLabel()
{
    auto sampler = currentSampler() ? currentSampler() : m_samplers.value(0);
    QString infoText;

    if (sampler)
    {
        infoText = (QString(
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
    }

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
    renderer.setDiscardFlags(QwtPlotRenderer::DiscardBackground
                             | QwtPlotRenderer::DiscardCanvasBackground);
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
    resize(1000, 562);
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

        tb->addWidget(make_vbox_container(QSL("Y-Scale"), yScaleCombo)
                      .container.release());
    }

    tb->addAction(QIcon(":/clear_histos.png"), QSL("Clear"), this, [this]() {
        if (m_d->m_currentIndex < 0) // combined view mode -> clear all samplers
        {
            for (auto &sampler: m_d->m_samplers)
                sampler->clearHistory(true);
            replot();
        }
        else if (auto sampler = m_d->m_samplers.value(m_d->m_currentIndex))
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

    // Enable the info area once we're fully constructed.
    QTimer::singleShot(0, this, [action]() { action->setChecked(true); });

    // Plot selection spinbox and Combined View checkbox
    {
        auto cb_combined = new QCheckBox;
        m_d->m_cb_combinedView = cb_combined;

        connect(cb_combined, &QCheckBox::stateChanged, this, [this] (int state) {

            m_d->m_spin_plotIndex->setEnabled(state == Qt::Unchecked);

            if (m_d->m_sink)
            {
                m_d->m_sink->setUseCombinedView(state == Qt::Checked);
            }

            if (state == Qt::Checked)
            {
                m_d->selectPlot(-1);
            }
            else
            {
                m_d->selectPlot(m_d->m_spin_plotIndex->value());
            }
        });

        auto spin_plotIndex = new QSpinBox;
        m_d->m_spin_plotIndex = spin_plotIndex;

        connect(spin_plotIndex, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
                this, [this] (int index) { m_d->selectPlot(index); });

        tb->addWidget(make_spacer_widget());

        auto c_w = new QWidget;
        auto c_l = new QGridLayout(c_w);
        c_l->setContentsMargins(0, 0, 0, 0);
        c_l->setSpacing(2);
        c_l->addWidget(new QLabel(QSL("Rate #")),   0, 0, Qt::AlignCenter);
        c_l->addWidget(spin_plotIndex,              1, 0, Qt::AlignCenter);
        c_l->addWidget(new QLabel(QSL("Combined")), 0, 1, Qt::AlignCenter);
        c_l->addWidget(cb_combined,                 1, 1, Qt::AlignCenter);

        tb->addWidget(c_w);
    }

    // Restore combined view state based on the setting stored in the sink.
    QTimer::singleShot(0, this, [this]() {
        if (m_d->m_sink)
            m_d->m_cb_combinedView->setChecked(m_d->m_sink->getUseCombinedView());
    });

    // Statusbar and info widgets
    m_d->m_statusBar = make_statusbar();
    m_d->m_labelCursorInfo = new QLabel;
    m_d->m_labelCursorInfoHelper = NonShrinkingLabelHelper(m_d->m_labelCursorInfo);

    m_d->m_labelVisibleRangeInfo = new QLabel;
    m_d->m_labelVisibleRangeInfoHelper = NonShrinkingLabelHelper(m_d->m_labelVisibleRangeInfo);

    m_d->m_labelPlotInfo = new QLabel;

    for (auto label: { m_d->m_labelCursorInfo, m_d->m_labelPlotInfo, m_d->m_labelVisibleRangeInfo})
    {
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        set_widget_font_pointsize(label, 7);
    }

    {
        m_d->m_infoContainer = new QWidget;

        auto layout = new QHBoxLayout(m_d->m_infoContainer);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        layout->addWidget(m_d->m_labelCursorInfo);
        layout->addWidget(make_separator_frame(Qt::Vertical));
        layout->addWidget(m_d->m_labelVisibleRangeInfo);
        layout->addWidget(make_separator_frame(Qt::Vertical));
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
        m_d->m_waterMarkText.setText(QString("mvme-%1").arg(mvme_git_version()));

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
    DO_AND_ASSERT(connect(zoomer, SIGNAL(zoomed(const QRectF &)),
                       this, SLOT(zoomerZoomed(const QRectF &))));
    DO_AND_ASSERT(connect(zoomer, &ScrollZoomer::mouseCursorMovedTo,
                       this, &RateMonitorWidget::mouseCursorMovedToPlotCoord));
    DO_AND_ASSERT(connect(zoomer, &ScrollZoomer::mouseCursorLeftPlot,
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
    : RateMonitorWidget(QVector<a2::RateSamplerPtr>{ sampler }, parent)
{
}

RateMonitorWidget::RateMonitorWidget(const QVector<a2::RateSamplerPtr> &samplers, QWidget *parent)
    : RateMonitorWidget(parent)
{
    assert(samplers.size());
    m_d->m_samplers = samplers;
    m_d->m_spin_plotIndex->setMinimum(0);
    m_d->m_spin_plotIndex->setMaximum(std::max(m_d->m_samplers.size() - 1, 0));
    m_d->m_spin_plotIndex->setVisible(m_d->m_samplers.size() > 0);
    m_d->selectPlot(0);
}

RateMonitorWidget::RateMonitorWidget(
    const SinkPtr &rms,
    SinkModifiedCallback sinkModifiedCallback,
    QWidget *parent)
    : RateMonitorWidget(parent)
{
    setSink(rms, sinkModifiedCallback);
}

RateMonitorWidget::~RateMonitorWidget()
{
}

void RateMonitorWidget::setSink(const SinkPtr &sink, SinkModifiedCallback sinkModifiedCallback)
{
    m_d->m_sink = sink;
    m_d->m_sinkModifiedCallback = sinkModifiedCallback;
    m_d->m_samplers = sink->getRateSamplers();
    m_d->m_plotWidget->setXScaleType(sink->getXScaleType());

    m_d->m_spin_plotIndex->setMinimum(0);
    m_d->m_spin_plotIndex->setMaximum(std::max(m_d->m_samplers.size() - 1, 0));
    m_d->m_spin_plotIndex->setVisible(m_d->m_samplers.size() > 0);

    m_d->selectPlot(0);

    if (sink->getUseCombinedView())
        m_d->m_cb_combinedView->setChecked(true);
    else
        m_d->m_spin_plotIndex->setValue(m_d->m_currentIndex);
}

void RateMonitorWidget::sinkModified()
{
    auto currentIndex = m_d->m_currentIndex;
    setSink(m_d->m_sink, m_d->m_sinkModifiedCallback);
    m_d->selectPlot(currentIndex);
}

void RateMonitorWidget::setPlotExportDirectory(const QDir &dir)
{
    m_d->m_exportDirectory = dir;
}

void RateMonitorWidget::replot()
{
    if (m_d->m_sink)
    {
        auto pathParts = analysis::make_parent_path_list(m_d->m_sink);
        pathParts.push_back(m_d->m_sink->objectName());
        setWindowTitle(pathParts.join('/'));

        auto &sink = m_d->m_sink;
        m_d->m_samplers = sink->getRateSamplers();
        m_d->m_spin_plotIndex->setMinimum(0);
        m_d->m_spin_plotIndex->setMaximum(std::max(m_d->m_samplers.size() - 1, 0));
        m_d->m_spin_plotIndex->setVisible(m_d->m_samplers.size() > 0);
    }

    m_d->m_plotWidget->replot();
    m_d->updateVisibleRangeInfoLabel();
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
