#include "daqstats_widget.h"
#include "mvme_context.h"
#include <QLabel>
#include <QFormLayout>
#include <QTimer>

static const int updateInterval = 500;

struct DAQStatsWidgetPrivate
{
    MVMEContext *context;

    QLabel *label_daqDuration,
           *label_freeBuffers,
           *label_buffersReadAndDropped,
           *label_mbPerSecond,
           *label_events,
           *label_readSize,
           *label_vmusbAvgEventsPerBuffer,
           *label_bytesRead,
           *label_listFileSize;
};

DAQStatsWidget::DAQStatsWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new DAQStatsWidgetPrivate)
{
    m_d->context = context;

    m_d->label_daqDuration = new QLabel;
    m_d->label_freeBuffers = new QLabel;
    m_d->label_buffersReadAndDropped = new QLabel("0 / 0");
    m_d->label_mbPerSecond = new QLabel;
    m_d->label_events = new QLabel;
    m_d->label_readSize = new QLabel;
    m_d->label_vmusbAvgEventsPerBuffer = new QLabel;
    m_d->label_bytesRead = new QLabel;
    m_d->label_listFileSize = new QLabel;

    QList<QWidget *> labels = {
        m_d->label_daqDuration,
        m_d->label_freeBuffers,
        m_d->label_buffersReadAndDropped,
        m_d->label_mbPerSecond,
        m_d->label_events,
        m_d->label_readSize,
        m_d->label_vmusbAvgEventsPerBuffer,
        m_d->label_bytesRead,
        m_d->label_listFileSize,
    };

    for (auto label: labels)
    {
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    }

    auto formLayout = new QFormLayout(this);

    formLayout->addRow("Running time:", m_d->label_daqDuration);
    formLayout->addRow("Free event buffers:", m_d->label_freeBuffers);
    formLayout->addRow("Buffers read / dropped / errors:", m_d->label_buffersReadAndDropped);
    formLayout->addRow("Buffers/s / MB/s:", m_d->label_mbPerSecond);
    formLayout->addRow("Events:", m_d->label_events);
    formLayout->addRow("Avg. read size:", m_d->label_readSize);
    formLayout->addRow("vmusb avg. events per buffer:", m_d->label_vmusbAvgEventsPerBuffer);
    formLayout->addRow("Bytes read:", m_d->label_bytesRead);
    formLayout->addRow("ListFile size:", m_d->label_listFileSize);

    //formLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    //formLayout->setSizeConstraint(QLayout::SetFixedSize);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    updateWidget();

    auto timer = new QTimer(this);
    timer->setInterval(updateInterval);
    timer->start();

    connect(timer, &QTimer::timeout, this, &DAQStatsWidget::updateWidget);
}

DAQStatsWidget::~DAQStatsWidget()
{
    delete m_d;
}

void DAQStatsWidget::updateWidget()
{
    auto stats = m_d->context->getDAQStats();

    auto startTime = stats.startTime;
    auto endTime   = m_d->context->getDAQState() == DAQState::Idle ? stats.endTime : QDateTime::currentDateTime();
    auto duration  = startTime.secsTo(endTime);
    auto durationString = makeDurationString(duration);

    double mbPerSecond = stats.bytesPerSecond / (1024.0 * 1024.0);
    double buffersPerSecond = stats.buffersPerSecond;

    m_d->label_daqDuration->setText(durationString);

    m_d->label_buffersReadAndDropped->setText(QString("%1 / %2 / %3")
                                              .arg(stats.totalBuffersRead)
                                              .arg(stats.droppedBuffers)
                                              .arg(stats.buffersWithErrors)
                                              );

    m_d->label_freeBuffers->setText(QString::number(stats.freeBuffers));
    m_d->label_readSize->setText(QString::number(stats.avgReadSize));
    m_d->label_mbPerSecond->setText(QString("%1 / %2")
                                    .arg(buffersPerSecond, 6, 'f', 2)
                                    .arg(mbPerSecond, 6, 'f', 2)
                                    );

    m_d->label_bytesRead->setText(
            QString("%1 MB")
            .arg((double)stats.totalBytesRead / (1024.0*1024.0), 6, 'f', 2)
            );

    m_d->label_events->setText(
        QString("%1 Events/s")
        .arg(stats.eventsPerSecond, 6, 'f', 2)
        );

    switch (m_d->context->getMode())
    {
        case GlobalMode::DAQ:
            {
                m_d->label_listFileSize->setText(
                        QString("%1 MB")
                        .arg((double)stats.listFileBytesWritten / (1024.0*1024.0), 6, 'f', 2)
                        );
            } break;
        case GlobalMode::ListFile:
            {
                m_d->label_listFileSize->setText(
                        QString("%1 MB")
                        .arg((double)stats.listFileTotalBytes / (1024.0*1024.0), 6, 'f', 2)
                        );
            } break;

        case GlobalMode::NotSet:
            break;
    }

    m_d->label_vmusbAvgEventsPerBuffer->setText(QString::number(stats.vmusbAvgEventsPerBuffer));
}
