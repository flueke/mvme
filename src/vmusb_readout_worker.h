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

#include "mvme_context.h"
#include "vmusb_stack.h"
#include <QObject>

class VMUSBBufferProcessor;

class VMUSBReadoutWorker: public QObject
{
    Q_OBJECT
    signals:
        void stateChanged(DAQState);
        void daqStopped();

    public:
        VMUSBReadoutWorker(MVMEContext *context, QObject *parent = 0);
        ~VMUSBReadoutWorker();

        void setBufferProcessor(VMUSBBufferProcessor *processor) { m_bufferProcessor = processor; }
        VMUSBBufferProcessor *getBufferProcessor() const { return m_bufferProcessor; }
        QString getLastErrorMessage() const { return m_errorMessage; }

        bool isRunning() const { return m_state != DAQState::Idle; }

    public slots:
        void start(quint32 cycles = 0);
        void stop();
        void pause();
        void resume();

    private:
        void readoutLoop();
        void setState(DAQState state);
        void logMessage(const QString &message);
        void logError(const QString &);
        void clearError() { m_errorMessage.clear(); }

        struct ReadBufferResult
        {
            int bytesRead;
            VMEError error;
        };

        ReadBufferResult readBuffer(int timeout_ms);

        MVMEContext *m_context;
        DAQState m_state = DAQState::Idle;
        DAQState m_desiredState = DAQState::Idle;
        quint32 m_cyclesToRun = 0;
        VMUSBStack m_vmusbStack;
        DataBuffer *m_readBuffer = 0;
        QMap<u8, u32> m_eventCountPerStack;
        size_t m_nTotalEvents;
        VMUSBBufferProcessor *m_bufferProcessor = 0;
        QString m_errorMessage;
        VMUSB *m_vmusb = nullptr;
};

#endif
