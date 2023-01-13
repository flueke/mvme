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
#ifndef __MVME_MVME_LISTFILE_WORKER_H__
#define __MVME_MVME_LISTFILE_WORKER_H__

#include "listfile_replay_worker.h"
#include "mvme_listfile_utils.h"

class LIBMVME_EXPORT MVMEListfileWorker: public ListfileReplayWorker
{
    Q_OBJECT
    public:
        using LoggerFun = std::function<void (const QString &)>;

        MVMEListfileWorker(
            ThreadSafeDataBufferQueue *emptyBufferQueue,
            ThreadSafeDataBufferQueue *filledBufferQueue,
            QObject *parent = nullptr);

        ~MVMEListfileWorker() override;

        void setListfile(ListfileReplayHandle *handle) override;

        DAQStats getStats() const override { return m_stats; }
        bool isRunning() const override { return m_state != DAQState::Idle; }
        DAQState getState() const override { return m_state; }

        void setEventsToRead(u32 eventsToRead) override;

    public slots:
        // Blocking call which will perform the work
        void start() override;

        // Thread-safe calls, setting internal flags to do the state transition
        void stop() override;
        void pause() override;
        void resume() override;

    private:
        void mainLoop();
        void setState(DAQState state);

        DAQStats m_stats;

        std::atomic<DAQState> m_state;
        std::atomic<DAQState> m_desiredState;

        ListFile m_listfile;

        qint64 m_bytesRead = 0;
        qint64 m_totalBytes = 0;

        u32 m_eventsToRead = 0;
        bool m_logBuffers = false;
};

#endif /* __MVME_MVME_LISTFILE_WORKER_H__ */
