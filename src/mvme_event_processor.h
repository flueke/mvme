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
#ifndef UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f
#define UUID_2aee2ea6_9760_46db_8d90_4dad1e4d019f

#include "typedefs.h"
#include "globals.h"
#include "data_buffer_queue.h"
#include <QHash>
#include <QObject>
#include <QVector>

class DataBuffer;
class MVMEContext;
class MesytecDiagnostics;
class DualWordDataFilterConfig;

class MVMEEventProcessorPrivate;

using DualWordFilterValues = QHash<DualWordDataFilterConfig *, u64>;
using DualWordFilterDiffs  = QHash<DualWordDataFilterConfig *, double>;

struct MVMEEventProcessorStats
{
    static const u32 MaxEvents = 12;
    static const u32 MaxModulesPerEvent = 20;

    QDateTime startTime;
    u64 bytesProcessed = 0;
    u32 buffersProcessed = 0;
    u32 buffersWithErrors = 0;
    u32 eventSections = 0;
    u32 invalidEventIndices = 0;
    using ModuleCounters = std::array<u32, MaxModulesPerEvent>;
    std::array<ModuleCounters, MaxEvents> moduleCounters;
    std::array<u32, MaxEvents> eventCounters;
};

class MVMEEventProcessor: public QObject
{
    Q_OBJECT
    signals:
        void started();
        void stopped();
        void stateChanged(EventProcessorState);

        void logMessage(const QString &);

    public:
        MVMEEventProcessor(MVMEContext *context);
        ~MVMEEventProcessor();

        void setDiagnostics(MesytecDiagnostics *diag);
        MesytecDiagnostics *getDiagnostics() const;

        EventProcessorState getState() const;
        MVMEEventProcessorStats getStats() const;

        void setListFileVersion(u32 version);

        ThreadSafeDataBufferQueue *m_freeBuffers = nullptr;
        ThreadSafeDataBufferQueue *m_fullBuffers = nullptr;

        void processDataBuffer(DataBuffer *buffer);
        void processEventSection(u32 sectionHeader, u32 *data, u32 size);

    public slots:
        void removeDiagnostics();
        void newRun(const RunInfo &runInfo);

        void startProcessing();
        void stopProcessing(bool whenQueueEmpty = true);

    private:
        MVMEEventProcessorPrivate *m_d;
};

#endif
