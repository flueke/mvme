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
#include "mvlc/mvlc_vme_controller.h"
#include "vme_daq.h"
#include "vme_readout_worker.h"

struct MVLCReadoutCounters
{
    // MVLC_USB only (ETH does not perform frame type checks because incoming
    // data is packetized as is).
    // The number of unexpected frame types encountered. The readout should
    // produce outer frames of types StackFrame and StackContinuation only.
    u64 frameTypeErrors;

    // MVLC_USB only.
    // Total number of bytes from partial frames. This is the amount of data
    // moved from the end of incoming read buffers into temp storage and then
    // reused at the start of the next buffer.
    u64 partialFrameTotalBytes;
};

class MVLCReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    signals:
        void debugInfoReady(const DataBuffer &buffer);

    public:
        MVLCReadoutWorker(QObject *parent = nullptr);
        ~MVLCReadoutWorker() override;

        void start(quint32 cycles = 0) override;
        void stop() override;
        void pause() override;
        void resume(quint32 cycles = 0) override;
        bool isRunning() const override;
        DAQState getState() const override;

        MVLCReadoutCounters getReadoutCounters() const;
        mesytec::mvme_mvlc::MVLC_VMEController *getMVLC();

    public slots:
        void requestDebugInfoOnNextBuffer();
        void requestDebugInfoOnNextError();

    private:
        struct Private;
        std::unique_ptr<Private> d;

        void setMVLCObjects();
        void readoutLoop();
        void setState(const DAQState &state);
        void logError(const QString &msg);
        void timetick();
        void pauseDAQ();
        void resumeDAQ();
        std::error_code readAndProcessBuffer(size_t &bytesTransferred);
        std::error_code readout_eth(size_t &bytesTransferred);
        std::error_code readout_usb(size_t &bytesTransferred);

        DataBuffer *getOutputBuffer();
        void maybePutBackBuffer();
        void flushCurrentOutputBuffer();
        //void handleStackErrorNotification(const QVector<u32> &data);
};

#endif /* __MVME_MVLC_READOUT_WORKER_H__ */
