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
#ifndef __MVME_LISTFILE_REPLAY_WORKER_H__
#define __MVME_LISTFILE_REPLAY_WORKER_H__

#include "libmvme_export.h"

#include "data_buffer_queue.h"
#include "listfile_replay.h"

class LIBMVME_EXPORT ListfileReplayWorker: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(const DAQState &);
        void replayStopped();
        void replayPaused();
        // Used by the mvlc replay worker to communicate when the next listfile
        // part is opened.
        void currentFilenameChanged(const QString &filename);

    public:
        using LoggerFun = std::function<void (const QString &)>;

        // Explicitly passing in the queues for empty and filled buffers.
        ListfileReplayWorker(
            ThreadSafeDataBufferQueue *emptyBufferQueue,
            ThreadSafeDataBufferQueue *filledBufferQueue,
            QObject *parent = nullptr);

        // No buffer queues passed. This is intended for the MVLCListfileWorker
        // which uses the mesytec-mvlc queues internally.
        explicit ListfileReplayWorker(
            QObject *parent = nullptr)
            : QObject(parent)
        { }

        void setLogger(LoggerFun logger);

        virtual void setListfile(ListfileReplayHandle *handle) = 0;
        virtual DAQStats getStats() const = 0;
        virtual bool isRunning() const = 0;
        virtual DAQState getState() const = 0;
        virtual void setEventsToRead(u32 eventsToRead) = 0;

    public slots:
        // Blocking call which will perform the work
        virtual void start() = 0;

        // Thread-safe calls, setting internal flags to do the state transition
        virtual void stop() = 0;
        virtual void pause() = 0;
        virtual void resume() = 0;

    protected:
        inline ThreadSafeDataBufferQueue *getEmptyQueue() { return m_emptyBufferQueue; }
        inline ThreadSafeDataBufferQueue *getFilledQueue() { return m_filledBufferQueue; }
        inline LoggerFun &getLogger() { return m_logger; }

        inline void logMessage(const QString &msg)
        {
            if (getLogger())
                getLogger()(msg);
        }

    private:
        ThreadSafeDataBufferQueue *m_emptyBufferQueue = nullptr;
        ThreadSafeDataBufferQueue *m_filledBufferQueue = nullptr;
        LoggerFun m_logger;
};

#endif /* __MVME_LISTFILE_REPLAY_WORKER_H__ */
