/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_MVLC_READOUT_WORKER_H__
#define __MVME_MVLC_READOUT_WORKER_H__

#include <mesytec-mvlc/mvlc_readout.h>

#include "libmvme_export.h"
#include "mvlc/mvlc_qt_object.h"
#include "mvlc/mvlc_vme_controller.h"
#include "vme_daq.h"
#include "vme_readout_worker.h"

class LIBMVME_EXPORT MVLCReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    signals:
        void debugInfoReady(const mesytec::mvlc::ReadoutBuffer &readoutBuffer);

    public:
        explicit MVLCReadoutWorker(QObject *parent = nullptr);
        ~MVLCReadoutWorker() override;

    public slots:
        void start(quint32 cycles = 0) override;
        void stop() override;
        void pause() override;
        void resume(quint32 cycles = 0) override;

    public:
        bool isRunning() const override;
        DAQState getState() const override;

        mesytec::mvlc::ReadoutWorker::Counters getReadoutCounters() const;
        mesytec::mvme_mvlc::MVLC_VMEController *getMVLC();

        void setSnoopQueues(mesytec::mvlc::ReadoutBufferQueues *snoopQueues);

    public slots:
        void requestDebugInfoOnNextBuffer();
        void requestDebugInfoOnNextError();

    private:
        struct Private;
        std::unique_ptr<Private> d;

        void setMVLCObjects();
        void logError(const QString &msg);
};

bool LIBMVME_EXPORT run_daq_start_sequence(
    mesytec::mvme_mvlc::MVLC_VMEController *mvlcCtrl,
    VMEConfig &vmeConfig,
    bool ignoreStartupErrors,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> error_logger);


#endif /* __MVME_MVLC_READOUT_WORKER_H__ */
