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
#ifndef __DAQCONTROL_H__
#define __DAQCONTROL_H__

#include <chrono>
#include <QObject>
#include <QTimer>

#include "libmvme_export.h"
#include "mvme_context.h"

class TimedRunControl;

// DAQ control abstraction: start, stop, pause, resume and status queries.
// Currently builds on top the central MVMEContext object.
class LIBMVME_EXPORT DAQControl: public QObject
{
    Q_OBJECT
    signals:
        void daqStateChanged(const DAQState &state);

    public:
        DAQControl(MVMEContext *context, QObject *parent = nullptr);
        ~DAQControl() override;

        DAQState getDAQState() const;

    public slots:
        void startDAQ(
            u32 nCycles, bool keepHistoContents,
            const std::chrono::milliseconds &runDuration = std::chrono::milliseconds::zero());
        void startDAQ() { startDAQ(0, false, {}); }
        void stopDAQ();
        void pauseDAQ();
        void resumeDAQ(u32 nCycles);

    private:
        MVMEContext *m_context;
        std::unique_ptr<TimedRunControl> m_timedRunControl;
};

// Helper to limit the duration of a DAQ run.
// Note: paused state does not cause this object to pause its internal timer.
// Instead after the timer expires the DAQ is stopped even if it is in paused
// state.
class LIBMVME_EXPORT TimedRunControl: public QObject
{
    Q_OBJECT

    public:
        explicit TimedRunControl(
            DAQControl *ctrl,
            const std::chrono::milliseconds &runDuration,
            QObject *parent = nullptr);

    private slots:
        void onDAQStateChanged(const DAQState &newState);
        void onTimerTimeout();

    private:
        DAQControl *m_ctrl;
        QTimer m_timer;
        bool m_shouldStop;
};

#endif /* __DAQCONTROL_H__ */
