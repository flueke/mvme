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
#include "threading.h"
#include "globals.h"
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

        // Returns a deep copy of the hash to avoid threading issues.
        DualWordFilterValues getDualWordFilterValues() const;

        // Returns a hash of the most recent differences of dual word filter values.
        DualWordFilterDiffs getDualWordFilterDiffs() const;

        EventProcessorState getState() const;

        void setListFileVersion(u32 version);

        ThreadSafeDataBufferQueue *m_freeBufferQueue = nullptr;
        ThreadSafeDataBufferQueue *m_filledBufferQueue = nullptr;

    public slots:
        void removeDiagnostics();
        void newRun(const RunInfo &runInfo);
        void processDataBuffer(DataBuffer *buffer);

        void startProcessing();
        void stopProcessing(bool whenQueueEmpty = true);

    private:
        MVMEEventProcessorPrivate *m_d;
};

#endif
