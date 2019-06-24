#include "analysis_info_widget.h"

#include <cmath>
#include <QFormLayout>
#include <QTimer>

#include "util/counters.h"
#include "util/strings.h"
#include "mvme_stream_worker.h"

struct AnalysisInfoWidgetPrivate
{
    MVMEContext *context;
    MVMEStreamProcessorCounters prevCounters;
    QDateTime lastUpdateTime;
    QVector<QLabel *> labels;
    QTimer updateTimer;
};

static const QVector<const char *> LabelTexts =
{
    "state",
    "started",
    "stopped",
    "elapsed",
    "throughput",
    "bytesProcessed",
    "buffersProcessed",
    "buffersWithErrors",
    "avgBufferSize",
    "eventSections",
    "invalid event index",
    "counts by event",
    "counts by module",
    "rate by event ",
    "rate by module",
};

AnalysisInfoWidget::AnalysisInfoWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisInfoWidgetPrivate)
{
    setFocusPolicy(Qt::StrongFocus);
    m_d->context = context;

    setWindowTitle(QSL("Analysis Info"));

    auto layout = new QFormLayout(this);
    for (const char *text: LabelTexts)
    {
        auto label = new QLabel;
        layout->addRow(text, label);
        m_d->labels.push_back(label);
    }

    update();

    m_d->updateTimer.setInterval(1000);
    m_d->updateTimer.start();

    connect(&m_d->updateTimer, &QTimer::timeout, this, &AnalysisInfoWidget::update);
    connect(context, &MVMEContext::mvmeStreamWorkerStateChanged, this, [this](MVMEStreamWorkerState state) {
        if (state == MVMEStreamWorkerState::Running)
        {
            m_d->prevCounters = {};
            m_d->lastUpdateTime = {};
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

    if (!streamWorker) return;

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

    u64 deltaBytesProcessed = calc_delta0(counters.bytesProcessed, m_d->prevCounters.bytesProcessed);
    u64 deltaBuffersProcessed = calc_delta0(counters.buffersProcessed, m_d->prevCounters.buffersProcessed);

    double bytesPerSecond   = deltaBytesProcessed / dt;
    double mbPerSecond      = bytesPerSecond / Megabytes(1);
    double buffersPerSecond = deltaBuffersProcessed / dt;
    double avgBufferSize    = deltaBytesProcessed / static_cast<double>(deltaBuffersProcessed);
    if (std::isnan(avgBufferSize)) avgBufferSize = 0.0;

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

    // elapsed
    m_d->labels[ii++]->setText(totalDurationString);

    // throughput
    m_d->labels[ii++]->setText(QString("%1 MB/s").arg(mbPerSecond));

    // bytesProcessed
    m_d->labels[ii++]->setText(QString("%1 MB")
                               .arg((double)counters.bytesProcessed / Megabytes(1), 6, 'f', 2));
    // buffersProcessed
    m_d->labels[ii++]->setText(QString("%1 buffers").arg(counters.buffersProcessed));

    // buffersWithErrors
    m_d->labels[ii++]->setText(QString("%1 buffers").arg(counters.buffersWithErrors));

    // avgBufferSize
    m_d->labels[ii++]->setText(QString("%1 bytes").arg(avgBufferSize));

    // eventSections
    m_d->labels[ii++]->setText(QString("%1 sections").arg(counters.eventSections));

    // invalid event index
    m_d->labels[ii++]->setText(QString("%1").arg(counters.invalidEventIndices));

    // counts by event
    m_d->labels[ii++]->setText(ecText);

    // counts by module
    m_d->labels[ii++]->setText(mcText);

    // rate by event
    m_d->labels[ii++]->setText(erText);

    // rate by module
    m_d->labels[ii++]->setText(mrText);

    m_d->prevCounters = counters;
    m_d->lastUpdateTime = QDateTime::currentDateTime();
}
