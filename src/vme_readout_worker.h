/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __VME_READOUT_WORKER_H__
#define __VME_READOUT_WORKER_H__

#include "data_buffer_queue.h"
#include "globals.h"
#include "vme_config.h"
#include "vme_controller.h"

struct VMEReadoutWorkerContext
{
    VMEController *controller;
    DAQStats *daqStats;
    VMEConfig *vmeConfig;
    ThreadSafeDataBufferQueue *freeBuffers,
                              *fullBuffers;
    ListFileOutputInfo *listfileOutputInfo;
    RunInfo *runInfo;

    std::function<void (const QString &)> logMessage;
    std::function<QStringList ()> getLogBuffer;
    std::function<QJsonDocument ()> getAnalysisJson;
};

class VMEReadoutWorker: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(DAQState);
        void daqStopped();
        void daqPaused();

    public:
        VMEReadoutWorker(QObject *parent = 0);

        void setContext(VMEReadoutWorkerContext context)
        {
            pre_setContext(context);
            m_workerContext = context;
        }
        inline const VMEReadoutWorkerContext &getContext() const { return m_workerContext; }
        inline VMEReadoutWorkerContext &getContext() { return m_workerContext; }

        inline ThreadSafeDataBufferQueue *getFreeQueue() { return m_workerContext.freeBuffers; }
        inline ThreadSafeDataBufferQueue *getFullQueue() { return m_workerContext.fullBuffers; }

        virtual bool isRunning() const = 0;
        virtual DAQState getState() const = 0;

    public slots:
        virtual void start(quint32 cycles = 0) = 0;
        virtual void stop() = 0;
        virtual void pause() = 0;
        virtual void resume(quint32 cycles = 0) = 0;

    protected:
        virtual void pre_setContext(VMEReadoutWorkerContext newContext) {}
        VMEReadoutWorkerContext m_workerContext;
};

#endif /* __VME_READOUT_WORKER_H__ */
