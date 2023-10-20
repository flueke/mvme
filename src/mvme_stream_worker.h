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
#ifndef UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f
#define UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f

#include "data_buffer_queue.h"
#include "globals.h"
#include "libmvme_export.h"
#include "mvme_stream_processor.h"
#include "typedefs.h"
#include "stream_worker_base.h"

#include <QHash>
#include <QObject>
#include <QVector>

class MesytecDiagnostics;
class VMEConfig;
struct MVMEStreamWorkerPrivate;

class LIBMVME_EXPORT MVMEStreamWorker: public StreamWorkerBase
{
    Q_OBJECT
    public:
        MVMEStreamWorker(
            ThreadSafeDataBufferQueue *freeBuffers,
            ThreadSafeDataBufferQueue *fullBuffers,
            QObject *parent = nullptr);

        ~MVMEStreamWorker() override;

        MVMEStreamProcessor *getStreamProcessor() const;

        void setDiagnostics(std::shared_ptr<MesytecDiagnostics> diag);
        bool hasDiagnostics() const;
        void setListFileVersion(u32 version);

        AnalysisWorkerState getState() const override;

        void setStartPaused(bool startPaused) override;
        bool getStartPaused() const override;

        MVMEStreamProcessorCounters getCounters() const override;

    public slots:
        void start() override;
        void stop(bool whenQueueEmpty = true) override;
        void pause() override;
        void resume() override;
        void singleStep() override;

        void startupConsumers() override;
        void shutdownConsumers() override;

        /* Is invoked from MVMEMainWindow via QMetaObject::invokeMethod so that
         * it runs in our thread. */
        void removeDiagnostics();

    private:
        void setState(AnalysisWorkerState newState);
        void logMessage(const QString &msg);

        friend struct MVMEStreamWorkerPrivate;
        MVMEStreamWorkerPrivate *m_d;
};

#endif
