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
#ifndef __SIS3153_READOUT_WORKER_H__
#define __SIS3153_READOUT_WORKER_H__

#include "vme_readout_worker.h"
#include "sis3153.h"
#include "vme_script.h"

/* FIXME: the readout worker currently only handles IRQ triggers. Fix this at
 * some later point (at least for the non-polling case). */

/* Stacklist based, IRQ only SIS3153 readout worker implementation. */
class SIS3153ReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    public:
        SIS3153ReadoutWorker(QObject *parent = 0);
        ~SIS3153ReadoutWorker();

        virtual void start(quint32 cycles) override;
        virtual void stop() override;
        virtual void pause() override;
        virtual void resume() override;
        virtual bool isRunning() const override;

    private:
        void setState(DAQState state);
        void readoutLoop();
        void logMessage(const QString &message);
        void logError(const QString &);

        void processReadoutResults(EventConfig *event, s32 eventConfigIndex,  const vme_script::ResultList &results);

        struct ReadBufferResult
        {
            int bytesRead;
            VMEError error;
        };

        void readoutLoop_cleanup();
        void timetick();
        ReadBufferResult readBuffer();

        void processSingleEventBuffer(
            u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size);

        void processMultiEventBuffer(
            u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size);


        DAQState m_state = DAQState::Idle;
        DAQState m_desiredState = DAQState::Idle;
        quint32 m_cyclesToRun = 0;
        DataBuffer m_localEventBuffer;
        DataBuffer m_readBuffer;
        SIS3153 *m_sis = nullptr;
        std::array<EventConfig *, 8> m_irqEventConfigs;
        std::array<s32, 8> m_irqEventConfigIndex;
        std::array<u64, 8> m_packetCountsByStack;
        QFile *m_debugFile = nullptr;
};

#endif /* __SIS3153_READOUT_WORKER_H__ */
