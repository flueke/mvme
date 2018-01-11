/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __SIS3153_READOUT_WORKER_H__
#define __SIS3153_READOUT_WORKER_H__

#include "mvme_stream_util.h"
#include "sis3153.h"
#include "vme_daq.h"
#include "vme_readout_worker.h"
#include "vme_script.h"

class SIS3153ReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    public:
        SIS3153ReadoutWorker(QObject *parent = 0);
        ~SIS3153ReadoutWorker();

        virtual void start(quint32 cycles = 0) override;
        virtual void stop() override;
        virtual void pause() override;
        virtual void resume(quint32 cycles = 0) override;
        virtual bool isRunning() const override;
        virtual DAQState getState() const override { return m_state; }

        struct Counters
        {
            std::array<u64, SIS3153Constants::NumberOfStackLists> stackListCounts;
            std::array<u64, SIS3153Constants::NumberOfStackLists> stackListBerrCounts_Block;
            std::array<u64, SIS3153Constants::NumberOfStackLists> stackListBerrCounts_Read;
            std::array<u64, SIS3153Constants::NumberOfStackLists> stackListBerrCounts_Write;

            u64 lostPackets = 0;
            u64 multiEventPackets = 0;
            int watchdogStackList = -1;

            Counters()
            {
                stackListCounts.fill(0);
                stackListBerrCounts_Block.fill(0);
                stackListBerrCounts_Read.fill(0);
                stackListBerrCounts_Write.fill(0);
            }
        };

        inline const Counters &getCounters() const
        {
            return m_counters;
        }

    private:
        void setState(DAQState state);
        void logMessage(const QString &message);
        void logError(const QString &);

        VMEError uploadStackList(u32 stackLoadAddress, QVector<u32> stackList);

        struct ReadBufferResult
        {
            int bytesRead;
            VMEError error;
        };

        // readout stuff
        void readoutLoop();
        void leaveDAQMode();
        ReadBufferResult readAndProcessBuffer();

        // mvme event processing

        /* Entry point for buffer processing. Called by readAndProcessBuffer() which then
         * dispatches to one of the process*Data() methods below. */
        void processBuffer(
            u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size);

        /* Handles the case where a multi event packet is received. The
         * assumption is that multi events and partials are never mixed. */
        u32 processMultiEventData(
            u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size);

        /* Handles the case where no partial event assembly is in progress and
         * the buffer contains a single complete event. */
        u32 processSingleEventData(
            u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size);

        /* Handles the case where partial event assembly is in progress or
         * should be started. */
        u32 processPartialEventData(
            u8 packetAck, u8 packetIdent, u8 packetStatus, u8 *data, size_t size);

        void timetick();

        DataBuffer *getOutputBuffer();

        struct ProcessingState
        {
            MVMEStreamWriterHelper streamWriter;
            /* If stackList is < 0 no partial event processing is in progress.
             * Otherwise a partial event for the stackList has been started. */
            s32 stackList = -1;
            s32 moduleIndex = -1;
            u8 expectedPacketStatus = 0u;
        };

        struct PacketLossCounter
        {
            PacketLossCounter(Counters *counters, VMEReadoutWorkerContext *rdoContext);

            inline u32 handlePacketNumber(s32 packetNumber, u64 bufferNumber);

            s32 m_lastReceivedPacketNumber;
            Counters *m_counters;
            VMEReadoutWorkerContext *m_rdoContext;
            bool m_leavingDAQ;
        };

        void flushCurrentOutputBuffer();
        void maybePutBackBuffer();
        void warnIfStreamWriterError(u64 bufferNumber, int writerFlags, u16 eventIndex);

        std::atomic<DAQState> m_state;
        std::atomic<DAQState> m_desiredState;
        quint32 m_cyclesToRun = 0;
        DataBuffer m_readBuffer;
        SIS3153 *m_sis = nullptr;
        std::array<EventConfig *, SIS3153Constants::NumberOfStackLists> m_eventConfigsByStackList;
        std::array<int, SIS3153Constants::NumberOfStackLists> m_eventIndexByStackList;
        Counters m_counters;
        u32 m_stackListControlRegisterValue = 0;
        int m_watchdogStackListIndex = -1;
        DataBuffer m_localEventBuffer;
        std::unique_ptr<DAQReadoutListfileHelper> m_listfileHelper;
        DataBuffer *m_outputBuffer = nullptr;
        ProcessingState m_processingState;
        PacketLossCounter m_lossCounter;
        QFile m_rawBufferOut;
        bool m_logBuffers = false;
};

#endif /* __SIS3153_READOUT_WORKER_H__ */
