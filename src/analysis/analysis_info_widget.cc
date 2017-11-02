#include "analysis_info_widget.h"

#include <QFormLayout>
#include <QTimer>

#include "mvme_event_processor.h"

struct AnalysisInfoWidgetPrivate
{
    MVMEContext *context;
    MVMEEventProcessorCounters prevCounters;
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
    "counts by module"
};

AnalysisInfoWidget::AnalysisInfoWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new AnalysisInfoWidgetPrivate)
{
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
    connect(context, &MVMEContext::eventProcessorStateChanged, this, [this](EventProcessorState state) {
        if (state == EventProcessorState::Running)
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
    EventProcessorState state = m_d->context->getEventProcessor()->getState();
    const auto &counters(m_d->context->getEventProcessor()->getCounters());

    auto startTime = counters.startTime;
    auto endTime   = state == EventProcessorState::Idle ? counters.stopTime : QDateTime::currentDateTime();
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

    auto calc_delta = [](u64 cur, u64 prev)
    {
        if (cur < prev)
            return (u64)0;
        return cur - prev;
    };

    u64 deltaBytesProcessed = calc_delta(counters.bytesProcessed, m_d->prevCounters.bytesProcessed);
    u64 deltaBuffersProcessed = calc_delta(counters.buffersProcessed, m_d->prevCounters.buffersProcessed);

    double bytesPerSecond   = deltaBytesProcessed / dt;
    double mbPerSecond      = bytesPerSecond / Megabytes(1);
    double buffersPerSecond = deltaBuffersProcessed / dt;
    double avgBufferSize    = deltaBytesProcessed / static_cast<double>(deltaBuffersProcessed);

    QString stateString = state == EventProcessorState::Idle ? QSL("Idle") : QSL("Running");

    QString ecText;
    QString mcText;

    for (u32 ei = 0; ei < MaxVMEEvents; ++ei)
    {
        for (u32 mi = 0; mi < MaxVMEModules; ++mi)
        {
            u32 count = counters.moduleCounters[ei][mi];
            if (count)
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

    s32 ii = 0;

    // state
    m_d->labels[ii++]->setText(stateString);
    // started
    m_d->labels[ii++]->setText(startTime.time().toString());
    // stopped
    if (state == EventProcessorState::Idle)
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

    m_d->prevCounters = counters;
    m_d->lastUpdateTime = QDateTime::currentDateTime();
}
