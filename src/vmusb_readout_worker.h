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
#ifndef UUID_29c99c43_ffae_4ead_8003_c89c87696c15
#define UUID_29c99c43_ffae_4ead_8003_c89c87696c15

#include "vme_readout_worker.h"
#include "vmusb_stack.h"

class VMUSBBufferProcessor;

class VMUSBReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    public:
        VMUSBReadoutWorker(QObject *parent = 0);
        ~VMUSBReadoutWorker();

        virtual void start(quint32 cycles = 0) override;
        virtual void stop() override;
        virtual void pause() override;
        virtual void resume() override;
        virtual bool isRunning() const override { return m_state != DAQState::Idle; }


    protected:
        virtual void pre_setContext(VMEReadoutWorkerContext newContext) override;

    private:
        void readoutLoop();
        void setState(DAQState state);
        void logMessage(const QString &message);
        void logError(const QString &);

        struct ReadBufferResult
        {
            int bytesRead;
            VMEError error;
        };

        ReadBufferResult readBuffer(int timeout_ms);

        DAQState m_state = DAQState::Idle;
        DAQState m_desiredState = DAQState::Idle;
        quint32 m_cyclesToRun = 0;
        VMUSBStack m_vmusbStack;
        DataBuffer *m_readBuffer = 0;
        QMap<u8, u32> m_eventCountPerStack;
        size_t m_nTotalEvents;
        VMUSBBufferProcessor *m_bufferProcessor = 0;
        VMUSB *m_vmusb = nullptr;
};

#endif
