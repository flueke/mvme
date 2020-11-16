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
#include "analysis_info_widget.h"

#include <cmath>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QTimer>

#include "util/counters.h"
#include "util/strings.h"
#include "mvlc_stream_worker.h"
#include "mvme_stream_worker.h"

using namespace mesytec;
using namespace mesytec::mvlc;

static const QVector<const char *> LabelTexts =
{
    "state",
    "started",
    "stopped",
    "elapsed",
    "bytesProcessed",
    "buffersProcessed",
    "total Events",
    "counts by event",
    "counts by module",
    "rate by event ",
    "rate by module",
};

static const QVector<const char *> MVLC_LabelTexts =
{
    "buffers",
    "internalBufferLoss",
    "unusedBytes",
    "ethPacketsProcessed",
    "ethPacketLoss",
    "systemEventTypes",
    "parseResults",
    "parserExceptions",
};

struct AnalysisInfoWidgetPrivate
{
    MVMEContext *context;
    MVMEStreamProcessorCounters prevCounters;
    QDateTime lastUpdateTime;
    QVector<QLabel *> labels;
    QTimer updateTimer;
    bool updateInProgress;
    QPushButton *mvlcRequestBufferOnError;
    QPushButton *mvlcRequestNextBuffer;

    QWidget *mvlcInfoWidget;
    QVector<QLabel *> mvlcLabels;
    mesytec::mvlc::readout_parser::ReadoutParserCounters prevMVLCCounters;

    void updateMVLCWidget(const mesytec::mvlc::readout_parser::ReadoutParserCounters &counters, double dt);
};

#if (QT_VERSION >= QT_VERSION_CHECK(5, 8, 0))
static const std::chrono::milliseconds WidgetUpdatePeriod(1000);
#else
static const int WidgetUpdatePeriod = 1000;
#endif

AnalysisInfoWidget::AnalysisInfoWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisInfoWidgetPrivate)
{
    setFocusPolicy(Qt::StrongFocus);
    m_d->context = context;

    setWindowTitle(QSL("Analysis Info"));

    // Upper layout for all VME controllers
    auto layout = new QFormLayout();
    for (const char *text: LabelTexts)
    {
        auto label = new QLabel;
        layout->addRow(text, label);
        m_d->labels.push_back(label);
    }

    // MVLC specific widgets
    m_d->mvlcInfoWidget = new QGroupBox("MVLC Readout Parser Counters:");
    {
        auto mvlcLayout = make_layout<QFormLayout, 0, 2>(m_d->mvlcInfoWidget);

        auto note = QSL(
            "<br><b>Note</b>: Internal buffer loss is caused by the analysis side not "
            "being able to keep up with the readout data rate. This internal "
            "loss can lead to readout parser errors (either non-ok parseResults "
            "or an increasing number of parserExceptions). Also internal buffer loss "
            "can lead to ethPacketLoss when using the MVLC_ETH which can cause more "
            "parser errors.<br/>"
            "<i>unusedBytes</i> is the number of bytes skipped by the readout parser.<br/>"
            "Use the debug buttons to see a snapshot of an incoming buffer and how the parser "
            " and multi event splitter are interpreting the data."
            );
        auto noteLabel = new QLabel(note);
        noteLabel->setWordWrap(true);


        for (const char *text: MVLC_LabelTexts)
        {
            auto label = new QLabel;
            label->setSizePolicy({
                QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding});
            mvlcLayout->addRow(text, label);
            m_d->mvlcLabels.push_back(label);
        }

        m_d->mvlcRequestBufferOnError = new QPushButton("Debug on parse error");
        m_d->mvlcRequestNextBuffer = new QPushButton("Debug next buffer");

        mvlcLayout->addRow(m_d->mvlcRequestBufferOnError);
        mvlcLayout->addRow(m_d->mvlcRequestNextBuffer);

        connect(m_d->mvlcRequestBufferOnError, &QPushButton::clicked,
                this, [this] ()
        {
            if (auto worker = qobject_cast<MVLC_StreamWorker *>(
                    m_d->context->getMVMEStreamWorker()))
            {
                worker->requestDebugInfoOnNextError();
            }
        });

        connect(m_d->mvlcRequestNextBuffer, &QPushButton::clicked,
                this, [this] ()
        {
            if (auto worker = qobject_cast<MVLC_StreamWorker *>(
                    m_d->context->getMVMEStreamWorker()))
            {
                worker->requestDebugInfoOnNextBuffer();
            }
        });

        mvlcLayout->addRow(noteLabel);
    }

    // outer widget layout
    auto outerLayout = new QVBoxLayout(this);
    outerLayout->addLayout(layout);
    outerLayout->addWidget(m_d->mvlcInfoWidget);
    outerLayout->addStretch(1);

    update();

    m_d->updateTimer.setSingleShot(true);
    m_d->updateTimer.start(WidgetUpdatePeriod);

    connect(&m_d->updateTimer, &QTimer::timeout, this, &AnalysisInfoWidget::update);

    connect(context, &MVMEContext::mvmeStreamWorkerStateChanged,
            this, [this](MVMEStreamWorkerState state) {

        if (state == MVMEStreamWorkerState::Running)
        {
            m_d->prevCounters = {};
            m_d->lastUpdateTime = {};
            m_d->prevMVLCCounters = {};
        }
    });
}

AnalysisInfoWidget::~AnalysisInfoWidget()
{
    delete m_d;
}

void AnalysisInfoWidget::update()
{
    auto streamWorker = m_d->context->getMVMEStreamWorker();

    if (!streamWorker)
    {
        qDebug() << __PRETTY_FUNCTION__ << "early return because streamWorker is nullptr";
        return;
    }

    auto mvlcWorker = qobject_cast<MVLC_StreamWorker *>(streamWorker);

    MVMEStreamWorkerState state = streamWorker->getState();
    const auto counters = streamWorker->getCounters();

    auto startTime = counters.startTime;
    auto endTime   = (state == MVMEStreamWorkerState::Idle
                      ? counters.stopTime
                      : QDateTime::currentDateTime());

    auto totalDuration_s = startTime.secsTo(endTime);
    auto totalDurationString = makeDurationString(totalDuration_s);

    double dt;

    if (m_d->lastUpdateTime.isValid())
    {
        dt = m_d->lastUpdateTime.msecsTo(endTime);
    }
    else
    {
        dt = startTime.msecsTo(endTime);
    }

    dt /= 1000.0;

    u64 deltaBytesProcessed = calc_delta0(counters.bytesProcessed,
                                          m_d->prevCounters.bytesProcessed);

    double bytesPerSecond   = deltaBytesProcessed / dt;
    double mbPerSecond      = bytesPerSecond / Megabytes(1);
    if (std::isnan(mbPerSecond)) mbPerSecond = 0.0;

    QString stateString = state == MVMEStreamWorkerState::Idle ? QSL("Idle") : QSL("Running");

    QString ecText;
    QString mcText;

    // absolute counts per event and per module
    for (u32 ei = 0; ei < MaxVMEEvents; ei++)
    {
        for (u32 mi = 0; mi < MaxVMEModules; mi++)
        {
            double count = counters.moduleCounters[ei][mi];

            if (count > 0.0)
            {
                if (!mcText.isEmpty()) mcText += "\n";
                mcText += (QString("event=%1, module=%2, count=%3")
                           .arg(ei).arg(mi).arg(count));
            }
        }

        u32 count = counters.eventCounters[ei];
        if (count)
        {
            if (!ecText.isEmpty()) ecText += "\n";
            ecText += QString("event=%1, count=%2").arg(ei).arg(count);
        }
    }

    // calculate deltas for events and modules
    std::array<double, MaxVMEEvents> eventRates;
    std::array<std::array<double, MaxVMEModules>, MaxVMEEvents> moduleRates;

    for (u32 ei = 0; ei < MaxVMEEvents; ei++)
    {
        double eventDelta = calc_delta0(counters.eventCounters[ei], m_d->prevCounters.eventCounters[ei]);
        eventRates[ei] = eventDelta / dt;

        for (u32 mi = 0; mi < MaxVMEModules; mi++)
        {
            double moduleDelta = calc_delta0(counters.moduleCounters[ei][mi],
                                             m_d->prevCounters.moduleCounters[ei][mi]);
            moduleRates[ei][mi] = moduleDelta / dt;
        }
    }

    // format the deltas
    QString erText;
    QString mrText;

    for (u32 ei = 0; ei < MaxVMEEvents; ei++)
    {
        for (u32 mi = 0; mi < MaxVMEModules; mi++)
        {
            double rate = moduleRates[ei][mi];

            if (rate > 0.0)
            {
                auto rateString =format_number(rate, QSL("cps"), UnitScaling::Decimal);

                if (!mrText.isEmpty()) mrText += "\n";

                mrText += (QString("event=%1, module=%2, rate=%3")
                           .arg(ei).arg(mi).arg(rateString));
            }
        }

        double rate = eventRates[ei];

        if (rate > 0.0)
        {
            auto rateString =format_number(rate, QSL("cps"), UnitScaling::Decimal);

            if (!erText.isEmpty()) erText += "\n";

            erText += QString("event=%1, rate=%2").arg(ei).arg(rateString);
        }
    }

    s32 ii = 0;

    // state
    m_d->labels[ii++]->setText(stateString);
    // started
    m_d->labels[ii++]->setText(startTime.time().toString());
    // stopped
    if (state == MVMEStreamWorkerState::Idle)
    {
        m_d->labels[ii++]->setText(endTime.time().toString());
    }
    else
    {
        m_d->labels[ii++]->setText(QString());
    }

    QString t;

    // elapsed
    m_d->labels[ii++]->setText(totalDurationString);

    // bytesProcessed
    t = QSL("total=%1, rate=%2 MB/s")
        .arg(format_number(counters.bytesProcessed, "B", UnitScaling::Binary, 0, 'f', 2))
        .arg(mbPerSecond)
        ;

    m_d->labels[ii++]->setText(t);

    // buffersProcessed
    if (mvlcWorker)
    {
        auto pc = mvlcWorker->getReadoutParserCounters();
        auto seenBuffers = pc.buffersProcessed;
        double totalBuffers = pc.buffersProcessed + pc.internalBufferLoss;
        double seenPercent = seenBuffers / totalBuffers * 100.0;
        if (std::isnan(seenPercent)) seenPercent = 0.0;

        t = QSL("%1 of %2 buffers, %3%")
            .arg(seenBuffers)
            .arg(totalBuffers)
            .arg(seenPercent)
            ;
    }
    else
    {
        t = QSL("%1 buffers").arg(counters.buffersProcessed);
        if (counters.buffersWithErrors)
            t += QSL(", errors=%1").arg(counters.buffersWithErrors);
    }

    m_d->labels[ii++]->setText(t);

    // total Events
    t = QSL("%1 events").arg(counters.totalEvents);

    if (counters.invalidEventIndices)
        t += QSL(" (%1 invalid)").arg(counters.invalidEventIndices);

    m_d->labels[ii++]->setText(t);

    // counts by event
    m_d->labels[ii++]->setText(ecText);

    // counts by module
    m_d->labels[ii++]->setText(mcText);

    // rate by event
    m_d->labels[ii++]->setText(erText);

    // rate by module
    m_d->labels[ii++]->setText(mrText);

    // system event types
    //m_d->labels[ii++]->setText(sysEventCountsText);

    if (mvlcWorker)
    {
        m_d->mvlcInfoWidget->setVisible(true);
        auto counters = mvlcWorker->getReadoutParserCounters();
        m_d->updateMVLCWidget(counters, dt);
        m_d->prevMVLCCounters = counters;
    }
    else
    {
        m_d->mvlcInfoWidget->setVisible(false);
    }

    m_d->prevCounters = counters;
    m_d->lastUpdateTime = QDateTime::currentDateTime();
    m_d->updateTimer.start(WidgetUpdatePeriod);
}

void AnalysisInfoWidgetPrivate::updateMVLCWidget(
    const mesytec::mvlc::readout_parser::ReadoutParserCounters &counters, double dt)
{
    using mesytec::mvlc::readout_parser::ReadoutParserCounters;

    auto &prevCounters = prevMVLCCounters;

    double bufferRate = calc_rate0<TYPE_AND_VAL(&ReadoutParserCounters::buffersProcessed)>(
        counters, prevCounters, dt);

    double bufferLossRate = calc_rate0<TYPE_AND_VAL(&ReadoutParserCounters::internalBufferLoss)>(
        counters, prevCounters, dt);

    double unusedBytesRate = calc_rate0<TYPE_AND_VAL(&ReadoutParserCounters::unusedBytes)>(
        counters, prevCounters, dt);

    double ethPacketRate = calc_rate0<TYPE_AND_VAL(&ReadoutParserCounters::ethPacketsProcessed)>(
        counters, prevCounters, dt);

    double ethPacketLossRate = calc_rate0<TYPE_AND_VAL(&ReadoutParserCounters::ethPacketLoss)>(
        counters, prevCounters, dt);

    auto parseResultRates = calc_rates0<std::vector<double>>(
        counters.parseResults.cbegin(), counters.parseResults.cend(),
        prevCounters.parseResults.cbegin(), dt);

    auto parserExceptionRate = calc_rate0<TYPE_AND_VAL(&ReadoutParserCounters::parserExceptions)>(
        counters, prevCounters, dt);

    QStringList texts;

    texts += QString("%1, rate=%2 buffers/s")
        .arg(counters.buffersProcessed)
        .arg(bufferRate, 0, 'f', 0);

    texts += QString("%1, rate=%2 buffers/s")
        .arg(counters.internalBufferLoss)
        .arg(bufferLossRate, 0, 'f', 0);

    texts += QString("%1, rate=%2")
        .arg(format_number(counters.unusedBytes, "bytes", UnitScaling::Binary, 0, 'f', 2))
        .arg(format_number(unusedBytesRate, "bytes/s", UnitScaling::Binary, 0, 'f', 0));

    texts += QString("%1, rate=%2 packets/s")
        .arg(counters.ethPacketsProcessed)
        .arg(ethPacketRate, 0, 'f', 0);

    texts += QString("%1, rate=%2 packets/s")
        .arg(counters.ethPacketLoss)
        .arg(ethPacketLossRate, 0, 'f', 0);

    // system event subtypes
    {
        QString buffer;

        for (size_t subtype=0; subtype<counters.systemEvents.size(); subtype++)
        {
            if (!counters.systemEvents[subtype])
                continue;

            if (!buffer.isEmpty())
                buffer += "\n";

            buffer += QString("%1 (0x%2): %3")
                .arg(QString::fromStdString(mvlc::system_event_type_to_string(subtype)))
                .arg(subtype, 2, 16, QLatin1Char('0'))
                .arg(counters.systemEvents[subtype]);
        }

        texts += buffer;
    }

    // parse result counts
    {
        QString buffer;

        for (size_t pr = 0; pr < counters.parseResults.size(); ++pr)
        {
            if (!counters.parseResults[pr])
                continue;

            if (!buffer.isEmpty())
                buffer += "\n";

            buffer += QString("%1: %2, rate=%3 results/s")
                .arg(mvlc::readout_parser::get_parse_result_name(
                        static_cast<readout_parser::ParseResult>(pr)))
                .arg(counters.parseResults[pr])
                //.arg(format_number(counters.parseResults[pr], "", UnitScaling::Decimal, 0, 'f', 0))
                .arg(parseResultRates[pr], 0, 'f', 0);
        }

        texts += buffer;
    }

    // parser exceptions
    texts += QString("%1, rate=%2 exceptions/s")
        .arg(counters.parserExceptions)
        .arg(parserExceptionRate);

    // Assign the stat texts to the labels
    for (int i = 0; i < std::min(mvlcLabels.size(), texts.size()); ++i)
    {
        mvlcLabels[i]->setText(texts[i]);
    }
}
