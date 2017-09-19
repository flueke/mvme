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
#ifndef UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44
#define UUID_289d9e07_4304_4a19_9cf1_bfd5d1d41e44

#include "vmusb_readout_worker.h"

#include <QObject>
#include <QMap>

class EventConfig;
class BufferIterator;
class ListFileWriter;
class VMUSB;

struct VMUSBBufferProcessorPrivate;
struct ProcessorState;

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    public:
        VMUSBBufferProcessor(VMUSBReadoutWorker *parent = 0);
        ~VMUSBBufferProcessor();

        void processBuffer(DataBuffer *buffer);
        void timetick();

        ThreadSafeDataBufferQueue *m_freeBufferQueue;
        ThreadSafeDataBufferQueue *m_filledBufferQueue;

        void setLogBuffers(bool b) { m_logBuffers = b; }
        void beginRun();
        void endRun();

    private:
        DataBuffer *getFreeBuffer();
        u32 processEvent(BufferIterator &inIter, DataBuffer *outputBuffer, ProcessorState *state,
                         u64 bufferNumber, u16 eventIndex, u16 numberOfEvents);
        DAQStats *getStats();
        void logMessage(const QString &message);
        void resetRunState(); // call this when a new DAQ run starts

        friend class VMUSBBufferProcessorPrivate;
        VMUSBBufferProcessorPrivate *m_d;

        QMap<int, EventConfig *> m_eventConfigByStackID;
        DataBuffer m_localEventBuffer;
        DataBuffer m_localTimetickBuffer;
        ListFileWriter *m_listFileWriter = nullptr;
        bool m_logBuffers = false;
        VMUSB *m_vmusb = nullptr;
};

#endif
