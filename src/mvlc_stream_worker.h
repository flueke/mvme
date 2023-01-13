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
#ifndef __MVLC_STREAM_WORKERS_H__
#define __MVLC_STREAM_WORKERS_H__

#include "mesytec-mvlc/mvlc_readout_parser.h"
#include "stream_worker_base.h"

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/mesy_vme_format_checker.h>

#include "libmvme_export.h"
#include "data_buffer_queue.h"
#include "multi_event_splitter.h"
#include "mesytec_diagnostics.h"
#include "mvlc/readout_parser_support.h"

class MVMEContext;

struct EventRecord
{
    struct ModuleData
    {
        QVector<u32> data;
    };

    int eventIndex;
    QVector<ModuleData> modulesData;
};

void begin_event_record(
    EventRecord &record, int eventIndex);

void record_module_part(
    EventRecord &record, int moduleIndex, const u32 *data, u32 size);

bool is_empty(const EventRecord::ModuleData &moduleData);

class MVLC_StreamWorker: public StreamWorkerBase
{
    Q_OBJECT
    signals:
        void debugInfoReady(
            const DataBuffer &buffer,
            const mesytec::mvlc::readout_parser::ReadoutParserState &parserState,
            const mesytec::mvlc::readout_parser::ReadoutParserCounters &parserCounters,
            const VMEConfig *vmeConfig,
            const analysis::Analysis *analysis);

        void singleStepResultReady(const EventRecord &eventRecord);

    public:
        MVLC_StreamWorker(
            MVMEContext *context,
            mesytec::mvlc::ReadoutBufferQueues &snoopQueues,
            QObject *parent = nullptr);

        ~MVLC_StreamWorker() override;

        AnalysisWorkerState getState() const override
        {
            std::unique_lock<std::mutex> guard(m_stateMutex);
            return m_state;
        }

        void setStartPaused(bool startPaused) override
        {
            m_startPaused = startPaused;
        }

        bool getStartPaused() const override
        {
            return m_startPaused;
        }

        void attachModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer) override
        {
            m_moduleConsumers.push_back(consumer);
        }

        void removeModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer) override
        {
            if (auto it = std::remove(std::begin(m_moduleConsumers), std::end(m_moduleConsumers), consumer);
                it != std::end(m_moduleConsumers))
            {
                m_moduleConsumers.erase(it);
            }
        }

        void attachBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer) override
        {
            m_bufferConsumers.push_back(consumer);
        }

        void removeBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer) override
        {
            if (auto it = std::remove(std::begin(m_bufferConsumers), std::end(m_bufferConsumers), consumer);
                it != std::end(m_bufferConsumers))
            {
                m_bufferConsumers.erase(it);
            }
        }

        virtual MVMEStreamProcessorCounters getCounters() const override
        {
            mesytec::mvlc::UniqueLock guard(m_countersMutex);
            return m_counters;
        }

        mesytec::mvlc::readout_parser::ReadoutParserCounters getReadoutParserCounters() const
        {
            return m_parserCountersSnapshot.copy();
        }

        mesytec::mvme::multi_event_splitter::Counters getMultiEventSplitterCounters() const
        {
            return m_multiEventSplitterCounters.copy();
        }

        mesytec::mvlc::EventBuilder::EventBuilderCounters getEventBuilderCounters() const
        {
            return m_eventBuilder.getCounters();
        }

        void setDiagnostics(std::shared_ptr<MesytecDiagnostics> diag) { m_diag = diag; }
        bool hasDiagnostics() const { return m_diag != nullptr; }

    public slots:
        void startupConsumers() override;
        void shutdownConsumers() override;

        void start() override;

        void stop(bool whenQueueEmpty = true) override;
        void pause() override;
        void resume() override;
        void singleStep() override;

        void requestDebugInfoOnNextBuffer(bool ignoreTimeticks = true)
        {
            m_debugInfoRequest =
                (ignoreTimeticks
                 ? DebugInfoRequest::OnNextBufferIgnoreTimeticks
                 : DebugInfoRequest::OnNextBuffer);
        }

        void requestDebugInfoOnNextError()
        {
            m_debugInfoRequest = DebugInfoRequest::OnNextError;
        }

        /* Is invoked from MVMEMainWindow via QMetaObject::invokeMethod so that
         * it runs in our thread. */
        void removeDiagnostics() { m_diag.reset(); }

    private:
        using UniqueLock = mesytec::mvlc::UniqueLock;

        // Used for mapping mvlc::readout_parser module indexes to mvme module
        // indexes so that "disabled" modules are handled correctly.
        using ModuleIndexMap = std::array<int, MaxVMEModules>;

        void setState(AnalysisWorkerState newState);

        // Used for the transition from non-Idle state to Idle state.
        enum StopFlag
        {
            StopImmediately,
            StopWhenQueueEmpty,
        };

        enum class DebugInfoRequest
        {
            None,
            OnNextBuffer,
            OnNextBufferIgnoreTimeticks,
            OnNextError,
        };

        void fillModuleIndexMaps(
            const VMEConfig *vmeConfig);

        void setupParserCallbacks(
            const RunInfo &runInfo,
            const VMEConfig *vmeConfig,
            analysis::Analysis *analysis);

        void processBuffer(
            const mesytec::mvlc::ReadoutBuffer *buffer,
            const VMEConfig *vmeConfig,
            const analysis::Analysis *analysis
            );

        void blockIfPaused();
        void publishStateIfSingleStepping();

        void logParserInfo(const mesytec::mvlc::readout_parser::ReadoutParserState &parser);

        MVMEContext *m_context = nullptr;

        std::vector<std::shared_ptr<IStreamModuleConsumer>> m_moduleConsumers;
        std::vector<std::shared_ptr<IStreamBufferConsumer>> m_bufferConsumers;

        mutable mesytec::mvlc::TicketMutex m_countersMutex;
        MVMEStreamProcessorCounters m_counters = {};

        // Per event mappings of readout_parser -> mvme module indexes.
        std::array<ModuleIndexMap, MaxVMEEvents> m_eventModuleIndexMaps;
        mesytec::mvlc::ReadoutBufferQueues &m_snoopQueues;
        mesytec::mvlc::readout_parser::ReadoutParserCallbacks m_parserCallbacks;
        mesytec::mvlc::readout_parser::ReadoutParserState m_parser;
        mesytec::mvlc::readout_parser::ReadoutParserCounters m_parserCounters;
        mutable mesytec::mvlc::Protected<mesytec::mvlc::readout_parser::ReadoutParserCounters>
            m_parserCountersSnapshot;

        // Note: std::condition_variable requires an std::mutex, that's why a
        // TicketMutex is not used here.
        // Note2: despite being atomic variables when using a
        // condition_variable the shared resource has to be protected by a
        // std::mutex.
        mutable std::mutex m_stateMutex;
        std::condition_variable m_stateCondVar;
        std::atomic<AnalysisWorkerState> m_state;
        std::atomic<AnalysisWorkerState> m_desiredState;

        std::atomic<bool> m_startPaused;
        std::atomic<StopFlag> m_stopFlag;

        std::atomic<DebugInfoRequest> m_debugInfoRequest;
        mesytec::mvme::multi_event_splitter::State m_multiEventSplitter;
        mutable mesytec::mvlc::Protected<mesytec::mvme::multi_event_splitter::Counters> m_multiEventSplitterCounters;
        mesytec::mvme::multi_event_splitter::Callbacks m_multiEventSplitterCallbacks;
        mesytec::mvlc::EventBuilder m_eventBuilder;
        mesytec::mvlc::Callbacks m_eventBuilderCallbacks;
        std::unique_ptr<mesytec::mvlc::FormatCheckerState> m_formatCheckerState;

        EventRecord m_singleStepEventRecord = {};

        std::shared_ptr<MesytecDiagnostics> m_diag;
};

mesytec::mvme_mvlc::VMEConfReadoutScripts LIBMVME_EXPORT
    collect_readout_scripts(const VMEConfig &vmeConfig);

#endif /* __MVLC_STREAM_WORKERS_H__ */
