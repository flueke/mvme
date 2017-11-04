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
#include "daqstats_widget.h"
#include "mvme_context.h"
#include <QLabel>
#include <QFormLayout>
#include <QTimer>

static const int UpdateInterval_ms = 1000;

struct DAQStatsWidgetPrivate
{
    MVMEContext *context;
    DAQStats prevStats;
    QDateTime lastUpdateTime;

    QLabel *label_daqDuration,
           *label_buffersRead,
           *label_buffersDropped,
           *label_buffersErrors,
           *label_mbPerSecond,
           *label_events,
           *label_readSize,
           *label_bytesRead,
           *label_listFileSize,
           *label_netBytesRead,
           *label_netBytesPerSecond;
};

DAQStatsWidget::DAQStatsWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new DAQStatsWidgetPrivate)
{
    m_d->context = context;

    m_d->label_daqDuration = new QLabel;
    m_d->label_buffersRead = new QLabel;
    m_d->label_buffersDropped = new QLabel;
    m_d->label_buffersErrors = new QLabel;
    m_d->label_mbPerSecond = new QLabel;
    m_d->label_events = new QLabel;
    m_d->label_readSize = new QLabel;
    m_d->label_bytesRead = new QLabel;
    m_d->label_listFileSize = new QLabel;
    m_d->label_netBytesRead = new QLabel;
    m_d->label_netBytesPerSecond = new QLabel;

    QList<QWidget *> labels = {
        m_d->label_daqDuration,
        m_d->label_buffersRead,
        m_d->label_buffersDropped,
        m_d->label_buffersErrors,
        m_d->label_mbPerSecond,
        m_d->label_events,
        m_d->label_readSize,
        m_d->label_bytesRead,
        m_d->label_listFileSize,
        m_d->label_netBytesRead,
        m_d->label_netBytesPerSecond,
    };

    for (auto label: labels)
    {
        label->setParent(this);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    }

    auto formLayout = new QFormLayout(this);

    formLayout->addRow("Running time:", m_d->label_daqDuration);
    formLayout->addRow("Buffers read:", m_d->label_buffersRead);
    formLayout->addRow("Buffers with errors:", m_d->label_buffersErrors);
    formLayout->addRow("Buffers dropped:", m_d->label_buffersDropped);
    formLayout->addRow("Buffers/s / MB/s:", m_d->label_mbPerSecond);
    //formLayout->addRow("Events:", m_d->label_events);
    formLayout->addRow("Avg. read size:", m_d->label_readSize);
    formLayout->addRow("Bytes read:", m_d->label_bytesRead);
    formLayout->addRow("Net Bytes read:", m_d->label_netBytesRead);
    formLayout->addRow("Net Bytes/s:", m_d->label_netBytesPerSecond);
    formLayout->addRow("ListFile size:", m_d->label_listFileSize);

    //formLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    //formLayout->setSizeConstraint(QLayout::SetFixedSize);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    updateWidget();

    auto timer = new QTimer(this);
    timer->setInterval(UpdateInterval_ms);
    timer->start();

    connect(timer, &QTimer::timeout, this, &DAQStatsWidget::updateWidget);
    connect(context, &MVMEContext::daqStateChanged, this, [this](const DAQState &state) {
        if (state == DAQState::Running)
        {
            m_d->prevStats = {};
            m_d->lastUpdateTime = {};
        }
    });
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

    u64 deltaBytesRead = calc_delta(stats.totalBytesRead, m_d->prevStats.totalBytesRead);
    u64 deltaBuffersRead = calc_delta(stats.totalBuffersRead, m_d->prevStats.totalBuffersRead);
    u64 deltaEventsRead = calc_delta(stats.totalEventsRead, m_d->prevStats.totalEventsRead);
    u64 deltaNetBytesRead = calc_delta(stats.totalNetBytesRead, m_d->prevStats.totalNetBytesRead);

    double bytesPerSecond   = deltaBytesRead / dt;
    double mbPerSecond      = bytesPerSecond / Megabytes(1);
    double buffersPerSecond = deltaBuffersRead / dt;
    double eventsPerSecond  = deltaEventsRead / dt;
    double avgReadSize      = deltaBytesRead / static_cast<double>(deltaBuffersRead);
    double netBytesPerSecond = deltaNetBytesRead / dt;
    double netMbPerSecond   = netBytesPerSecond / Megabytes(1);



    m_d->label_daqDuration->setText(totalDurationString);

    m_d->label_buffersRead->setText(QString::number(stats.totalBuffersRead));
    m_d->label_buffersDropped->setText(QString::number(stats.droppedBuffers));
    m_d->label_buffersErrors->setText(QString::number(stats.buffersWithErrors));

    m_d->label_readSize->setText(QString::number(avgReadSize));
    m_d->label_mbPerSecond->setText(QString("%1 / %2")
                                    .arg(buffersPerSecond, 6, 'f', 2)
                                    .arg(mbPerSecond, 6, 'f', 2)
                                    );

    m_d->label_netBytesRead->setText(QString("%1 MB").arg(
            ((double)stats.totalNetBytesRead) / Megabytes(1), 6, 'f', 2));
    m_d->label_netBytesPerSecond->setText(QString("%1 MB/s").arg(
            netMbPerSecond, 6, 'f', 2));

    m_d->label_bytesRead->setText(
            QString("%1 MB")
            .arg((double)stats.totalBytesRead / (1024.0*1024.0), 6, 'f', 2)
            );

    /*
    m_d->label_events->setText(
        QString("%1 Events/s")
        .arg(eventsPerSecond, 6, 'f', 2)
        );
    */

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

    m_d->prevStats = stats;
    m_d->lastUpdateTime = QDateTime::currentDateTime();
}
