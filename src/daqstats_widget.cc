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
#include <QLabel>
#include <QFormLayout>
#include <QTimer>

#include "mvme_context.h"
#include "qt_util.h"
#include "sis3153_readout_worker.h"
#include "util/counters.h"

static const int UpdateInterval_ms = 1000;

struct DAQStatsWidgetPrivate
{
    MVMEContext *context;
    DAQStats prevStats;
    QDateTime lastUpdateTime;

    QLabel *label_daqDuration,
           *label_buffersRead,
           *label_bufferErrors,
           *label_bufferRates,
           *label_readSize,
           *label_bytesRead,
           //*label_listFileSize,
           *label_netBytesRead,
           *label_netRate;
};

DAQStatsWidget::DAQStatsWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new DAQStatsWidgetPrivate)
{
    m_d->context = context;

    //set_widget_font_pointsize(this, 8);

    m_d->label_daqDuration = new QLabel;
    m_d->label_buffersRead = new QLabel;
    m_d->label_bufferErrors = new QLabel;
    m_d->label_bufferRates = new QLabel;
    m_d->label_readSize = new QLabel;
    m_d->label_bytesRead = new QLabel;
    //m_d->label_listFileSize = new QLabel;
    //
    m_d->label_netBytesRead = new QLabel;
    m_d->label_netBytesRead->setToolTip(
        QSL("The number of bytes read excluding protocol overhead.\n"
            "This should be a measure for the amount of data the VME bus transferred."));
    m_d->label_netBytesRead->setStatusTip(m_d->label_netBytesRead->toolTip());

    m_d->label_netRate = new QLabel;

    QList<QWidget *> labels = {
        m_d->label_daqDuration,
        m_d->label_buffersRead,
        m_d->label_bufferErrors,
        m_d->label_bufferRates,
        m_d->label_readSize,
        m_d->label_bytesRead,
        //m_d->label_listFileSize,
        m_d->label_netBytesRead,
        m_d->label_netRate,
    };

    for (auto label: labels)
    {
        label->setParent(this);
        label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    }

    auto formLayout = new QFormLayout(this);

    formLayout->addRow("Running time:", m_d->label_daqDuration);
    formLayout->addRow("Buffers read:", m_d->label_buffersRead);
    formLayout->addRow("Buffer errors:", m_d->label_bufferErrors);
    formLayout->addRow("Buffer rates:", m_d->label_bufferRates);
    formLayout->addRow("Avg. read size:", m_d->label_readSize);
    formLayout->addRow("Bytes read:", m_d->label_bytesRead);
    formLayout->addRow("Net Bytes read:", m_d->label_netBytesRead);
    formLayout->addRow("Net rate:", m_d->label_netRate);
    //formLayout->addRow("ListFile size:", m_d->label_listFileSize);

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
    auto daqStats = m_d->context->getDAQStats();

    auto startTime = daqStats.startTime;
    auto endTime   = (m_d->context->getDAQState() == DAQState::Idle
                      ? daqStats.endTime
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

    u64 deltaBytesRead = calc_delta0(daqStats.totalBytesRead, m_d->prevStats.totalBytesRead);
    u64 deltaBuffersRead = calc_delta0(daqStats.totalBuffersRead, m_d->prevStats.totalBuffersRead);
    u64 deltaNetBytesRead = calc_delta0(daqStats.totalNetBytesRead, m_d->prevStats.totalNetBytesRead);

    double bytesPerSecond   = deltaBytesRead / dt;
    double mbPerSecond      = bytesPerSecond / Megabytes(1);
    double buffersPerSecond = deltaBuffersRead / dt;
    double avgReadSize      = deltaBytesRead / static_cast<double>(deltaBuffersRead);
    double netBytesPerSecond = deltaNetBytesRead / dt;
    double netMbPerSecond   = netBytesPerSecond / Megabytes(1);

    u64 bufferParseErrors = daqStats.buffersWithErrors;
    u64 lostBuffers = 0;

    if (auto rdoWorker = qobject_cast<SIS3153ReadoutWorker *>(m_d->context->getReadoutWorker()))
    {
        lostBuffers = rdoWorker->getCounters().lostPackets;
    }

    double totalBufferErrors = bufferParseErrors + lostBuffers;

    m_d->label_daqDuration->setText(totalDurationString);

    m_d->label_buffersRead->setText(QString::number(daqStats.totalBuffersRead));

    // errors
    m_d->label_bufferErrors->setText(QString::number(totalBufferErrors));
    m_d->label_bufferErrors->setToolTip(QString("parseErrors: %1, lost: %2")
                                          .arg(bufferParseErrors)
                                          .arg(lostBuffers));

    m_d->label_readSize->setText(QString::number(avgReadSize));
    m_d->label_bufferRates->setText(QString("%1 buffers/s, %2 MB/s")
                                    .arg(buffersPerSecond, 6, 'f', 2)
                                    .arg(mbPerSecond, 6, 'f', 2)
                                    );

    m_d->label_netBytesRead->setText(QString("%1 MB").arg(
            ((double)daqStats.totalNetBytesRead) / Megabytes(1), 6, 'f', 2));
    m_d->label_netRate->setText(QString("%1 MB/s").arg(
            netMbPerSecond, 6, 'f', 2));

    m_d->label_bytesRead->setText(
            QString("%1 MB")
            .arg((double)daqStats.totalBytesRead / (1024.0*1024.0), 6, 'f', 2)
            );

#if 0
    switch (m_d->context->getMode())
    {
        case GlobalMode::DAQ:
            {
                m_d->label_listFileSize->setText(
                        QString("%1 MB")
                        .arg((double)daqStats.listFileBytesWritten / (1024.0*1024.0), 6, 'f', 2)
                        );
            } break;
        case GlobalMode::ListFile:
            {
                m_d->label_listFileSize->setText(
                        QString("%1 MB")
                        .arg((double)daqStats.listFileTotalBytes / (1024.0*1024.0), 6, 'f', 2)
                        );
            } break;
    }
#endif

    m_d->prevStats = daqStats;
    m_d->lastUpdateTime = QDateTime::currentDateTime();
}
