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
#ifndef __SIS3153_READOUT_WORKER_H__
#define __SIS3153_READOUT_WORKER_H__

#include "mvme_stream_util.h"
#include "sis3153.h"
#include "vme_daq.h"
#include "vme_readout_worker.h"
#include "vme_script.h"

#include <QHostAddress>

class QUdpSocket;

class SIS3153ReadoutWorker: public VMEReadoutWorker
{
    Q_OBJECT
    public:
        explicit SIS3153ReadoutWorker(QObject *parent = 0);
        ~SIS3153ReadoutWorker();

        virtual void start(quint32 cycles = 0) override;
        virtual void stop() override;
        virtual void pause() override;
        virtual void resume(quint32 cycles = 0) override;
        virtual bool isRunning() const override;
        virtual DAQState getState() const override { return m_state; }

        using StackListCountsArray = std::array<u64, SIS3153Constants::NumberOfStackLists>;

        struct Counters
        {
            /* The number of complete events by stacklist. Extracted from the
             * "packetAck" byte.
             * Partial fragments are not counted but fully reassembled partial
             * events are.
             */
            StackListCountsArray stackListCounts;

            /* Error counters for VME BLT, read and write operations. Extracted
             * from the endHeader succeeding all events. */
            StackListCountsArray stackListBerrCounts_Block;
            StackListCountsArray stackListBerrCounts_Read;
            StackListCountsArray stackListBerrCounts_Write;

            /* The number of lost events. Counted using the beginHeader (0xbb..)
             * preceding all events. */
            u64 lostEvents = 0;

            /* The number of multievent packets received. */
            u64 multiEventPackets = 0;

            /* Number of events that are considered stale by the EventLossCounter. */
            u64 staleEvents = 0;

            /* The number of embedded events extracted from multievent packets. */
            StackListCountsArray embeddedEvents;

            /* The number of partial packets received. */
            StackListCountsArray partialFragments;

            /* The number of events that have been reassembled out of partial
             * fragments. */
            StackListCountsArray reassembledPartials;

            /* The number of the stacklist that's used for the watchdog. -1 if
             * no watchdog stacklist has been setup. */
            s32 watchdogStackList = -1;

            Counters()
            {
                stackListCounts.fill(0);
                stackListBerrCounts_Block.fill(0);
                stackListBerrCounts_Read.fill(0);
                stackListBerrCounts_Write.fill(0);
                embeddedEvents.fill(0);
                partialFragments.fill(0);
                reassembledPartials.fill(0);
            }

            u64 receivedEventsExcludingWatchdog() const;
            u64 receivedEventsIncludingWatchdog() const;
            u64 receivedWatchdogEvents() const;
        };

        inline const Counters &getCounters() const
        {
            return m_counters;
        }

    private:
        void setState(DAQState state);
        void logError(const QString &);

        VMEError uploadStackList(u32 stackLoadAddress, QVector<u32> stackList);

        struct ReadBufferResult
        {
            int bytesRead;
            VMEError error;
        };

        // readout stuff
        void readoutLoop();
        void enterDAQMode(u32 stackListControlValue);
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
            MVMEStreamWriterHelper streamWriter = MVMEStreamWriterHelper();
            /* If stackList is < 0 no partial event processing is in progress.
             * Otherwise a partial event for the stackList has been started. */
            s32 stackList = -1;
            s32 moduleIndex = -1;
            u8 expectedPacketStatus = 0u;
        };

        struct EventLossCounter
        {
            using Flags = u8;
            static const Flags Flag_None        = 0u;
            static const Flags Flag_IsStaleData = 1u << 0;
            static const Flags Flag_LeavingDAQ  = 1u << 1;

            EventLossCounter(Counters *counters, VMEReadoutWorkerContext *rdoContext);

            inline Flags handleEventSequenceNumber(s32 seqNum, u64 bufferNumber);
            void beginLeavingDAQ();
            void endLeavingDAQ();
            bool isLeavingDAQ() { return currentFlags & Flag_LeavingDAQ; }

            Counters *counters;
            VMEReadoutWorkerContext *rdoContext;
            s32 lastReceivedSequenceNumber;
            Flags currentFlags;
        };

        void flushCurrentOutputBuffer();
        void maybePutBackBuffer();
        void warnIfStreamWriterError(u64 bufferNumber, int writerFlags, u16 eventIndex);
        void setupUDPForwarding();

        std::atomic<DAQState> m_state;
        std::atomic<DAQState> m_desiredState;
        quint32 m_cyclesToRun = 0;
        DataBuffer m_readBuffer;
        SIS3153 *m_sis = nullptr;
        std::array<EventConfig *, SIS3153Constants::NumberOfStackLists> m_eventConfigsByStackList;
        std::array<int, SIS3153Constants::NumberOfStackLists> m_eventIndexByStackList;
        Counters m_counters;

        // Saves the state of the stack list control register as computed by the DAQ start
        // sequence. This is used to restore the correct value on resuming the DAQ from
        // pause.
        u32 m_stackListControlRegisterValue = 0;

        // Set at the beginning of the DAQ start sequence. During DAQ operation the set
        // bits should be kept active, other bits may be modified via stacklists by the
        // controller itself.
        // This is used to turn on OUT2 during execution of the first non-timer,
        // non-watchdog stacklist.
        u32 m_lemoIORegDAQBaseValue = 0;

        int m_watchdogStackListIndex = -1;
        DataBuffer m_localEventBuffer;
        std::unique_ptr<DAQReadoutListfileHelper> m_listfileHelper;
        DataBuffer *m_outputBuffer = nullptr;
        ProcessingState m_processingState;
        EventLossCounter m_lossCounter;
        QFile m_rawBufferOut;
        bool m_logBuffers = false;

        struct ForwardData
        {
            std::unique_ptr<QUdpSocket> socket;
            QHostAddress host;
            u16 port = 0;
        };

        ForwardData m_forward;
};

#endif /* __SIS3153_READOUT_WORKER_H__ */
