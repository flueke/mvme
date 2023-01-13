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
#ifndef UUID_29c99c43_ffae_4ead_8003_c89c87696c15
#define UUID_29c99c43_ffae_4ead_8003_c89c87696c15

#include <QFile>
#include "vme_readout_worker.h"
#include "vmusb_stack.h"

class VMUSBBufferProcessor;

class VMUSBReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    public:
        explicit VMUSBReadoutWorker(QObject *parent = 0);
        ~VMUSBReadoutWorker();

        virtual void start(quint32 cycles = 0) override;
        virtual void stop() override;
        virtual void pause() override;
        virtual void resume(quint32 cycles = 0) override;
        virtual bool isRunning() const override { return m_state != DAQState::Idle; }
        virtual DAQState getState() const override { return m_state; }

        void enableWriteRawBuffers(bool enabled);


    protected:
        virtual void pre_setContext(VMEReadoutWorkerContext newContext) override;

    private:
        void readoutLoop();
        void setState(DAQState state);
        void logError(const QString &);

        struct ReadBufferResult
        {
            int bytesRead;
            VMEError error;
        };

        ReadBufferResult readBuffer(int timeout_ms);

        std::atomic<DAQState> m_state;
        std::atomic<DAQState> m_desiredState;
        quint32 m_cyclesToRun = 0;
        VMUSBStack m_vmusbStack;
        DataBuffer *m_readBuffer = 0;
        QMap<u8, u32> m_eventCountPerStack;
        size_t m_nTotalEvents;
        VMUSBBufferProcessor *m_bufferProcessor = 0;
        VMUSB *m_vmusb = nullptr;
        QFile m_rawBufferOut;

        // Values to be set for LEDSrcRegister before
        // entering daq mode. After leaving DAQ mode (pause, stop) and any
        // remaining data has been read the register will be reset to 0.
        u32 m_daqLedSources = 0;
};

#endif
