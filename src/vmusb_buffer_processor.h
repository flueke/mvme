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

#include "databuffer.h"
#include "threading.h"

#include <QObject>
#include <QMap>
#include <QFile>

class DataBuffer;
class MVMEContext;
class EventConfig;
class BufferIterator;
class DAQStats;
class ListFileWriter;
class VMUSB;

struct VMUSBBufferProcessorPrivate;
struct ProcessorState;

class VMUSBBufferProcessor: public QObject
{
    Q_OBJECT
    public:
        VMUSBBufferProcessor(MVMEContext *context, QObject *parent = 0);

        void processBuffer(DataBuffer *buffer);

        ThreadSafeDataBufferQueue *m_freeBufferQueue;
        ThreadSafeDataBufferQueue *m_filledBufferQueue;

    public slots:
        void setLogBuffers(bool b) { m_logBuffers = b; }
        void beginRun();
        void endRun();
        void resetRunState(); // call this when a new DAQ run starts

    private:
        DataBuffer *getFreeBuffer();
        u32 processEvent(BufferIterator &inIter, DataBuffer *outputBuffer, ProcessorState *state,
                         u64 bufferNumber, u16 eventIndex, u16 numberOfEvents);
        DAQStats *getStats();
        void logMessage(const QString &message);

        friend class VMUSBBufferProcessorPrivate;
        VMUSBBufferProcessorPrivate *m_d;

        MVMEContext *m_context = nullptr;
        QMap<int, EventConfig *> m_eventConfigByStackID;
        DataBuffer m_localEventBuffer;
        ListFileWriter *m_listFileWriter = nullptr;
        bool m_logBuffers = false;
        VMUSB *m_vmusb = nullptr;
};

#endif
