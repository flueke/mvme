#include "analysis_info_widget.h"

#include <QFormLayout>
#include <QTimer>

#include "mvme_event_processor.h"

struct AnalysisInfoWidgetPrivate
{
    MVMEContext *context;
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

    connect(&m_d->updateTimer, &QTimer::timeout, this, &AnalysisInfoWidget::update);
    m_d->updateTimer.setInterval(1000);
    m_d->updateTimer.start();
    update();
}

AnalysisInfoWidget::~AnalysisInfoWidget()
{
    delete m_d;
}

void AnalysisInfoWidget::update()
{
    EventProcessorState state = m_d->context->getEventProcessor()->getState();
    const auto &counters(m_d->context->getEventProcessor()->getCounters());

    QString stateString;
    double secsElapsed = 0.0;

    switch (state)
    {
        case EventProcessorState::Idle:
            stateString = QSL("Idle");
            secsElapsed = counters.startTime.msecsTo(counters.stopTime) / 1000.0;
            break;
        case EventProcessorState::Running:
            stateString = QSL("Running");
            secsElapsed = counters.startTime.msecsTo(QDateTime::currentDateTime()) / 1000.0;
            break;
    }

    double mbRead = counters.bytesProcessed / (1024.0 * 1024.0);
    double mbPerSec = secsElapsed != 0.0 ? mbRead / secsElapsed : 0.0;

    QString ecText;
    QString mcText;

    for (u32 ei = 0; ei < MVMEEventProcessorCounters::MaxEvents; ++ei)
    {
        for (u32 mi = 0; mi < MVMEEventProcessorCounters::MaxModulesPerEvent; ++mi)
        {
            u32 count = counters.moduleCounters[ei][mi];
            if (count)
            {
                if (!mcText.isEmpty()) mcText += "\n";
                mcText += (QString("(event=%1, module=%2, count=%3)")
                           .arg(ei).arg(mi).arg(count));
            }
        }

        u32 count = counters.eventCounters[ei];
        if (count)
        {
            if (!ecText.isEmpty()) ecText += ", ";
            ecText += QString("(event=%1, count=%2)").arg(ei).arg(count);
        }
    }

    s32 ii = 0;

    m_d->labels[ii++]->setText(stateString);
    m_d->labels[ii++]->setText(counters.startTime.time().toString());
    m_d->labels[ii++]->setText(counters.stopTime.time().toString());
    m_d->labels[ii++]->setText(QString("%1 s").arg(secsElapsed));
    m_d->labels[ii++]->setText(QString("%1 MB/s").arg(mbPerSec));
    m_d->labels[ii++]->setText(QString("%1 MB").arg(mbRead));
    m_d->labels[ii++]->setText(QString("%1 buffers").arg(counters.buffersProcessed));
    m_d->labels[ii++]->setText(QString("%1 buffers").arg(counters.buffersWithErrors));
    m_d->labels[ii++]->setText(QString("%1 sections").arg(counters.eventSections));
    m_d->labels[ii++]->setText(QString("%1").arg(counters.invalidEventIndices));
    m_d->labels[ii++]->setText(ecText);
    m_d->labels[ii++]->setText(mcText);
}
