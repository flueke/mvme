/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_STREAM_PROCESSOR_H__
#define __MVME_STREAM_PROCESSOR_H__

#include "globals.h"
#include "libmvme_export.h"
#include "stream_processor_counters.h"
#include "stream_processor_consumers.h"

#include <QDateTime>
#include <QString>
#include <array>

namespace analysis
{
class Analysis;
}

struct DataBuffer;
class MesytecDiagnostics;
class VMEConfig;


struct MVMEStreamProcessorPrivate;

class LIBMVME_EXPORT MVMEStreamProcessor
{
    public:
        using Logger = std::function<void (const QString &)>;

        MVMEStreamProcessor();
        ~MVMEStreamProcessor();

        // Invokes startup() on attached module and buffer consumers.
        void startup();

        // Invokes shutdown() on attached module and buffer consumers.
        void shutdown();

        //
        // Statistics
        //
        MVMEStreamProcessorCounters getCounters() const;
        MVMEStreamProcessorCounters &getCounters();

        //
        // Processing
        //
        void beginRun(const RunInfo &runInfo, analysis::Analysis *analysis,
                      VMEConfig *vmeConfig, u32 listfileVersion, Logger logger);
        void endRun(const DAQStats &stats);
        void processDataBuffer(DataBuffer *buffer);

        // Used in DAQ Readout mode to generate timeticks for the analysis
        // independent of the readout data rate or analysis efficiency.
        void processExternalTimetick();


        //
        // Single Step Processing
        //

        /* Contains information about what was processed in the last call to
         * singleStepNextStep(). */
        struct ProcessingState
        {
            explicit ProcessingState(DataBuffer *buffer = nullptr)
                : buffer(buffer)
            {
                resetModuleDataOffsets();
            }

            void resetModuleDataOffsets()
            {
                lastModuleDataSectionHeaderOffsets.fill(-1);
                lastModuleDataBeginOffsets.fill(-1);
                lastModuleDataEndOffsets.fill(-1);
            }

            /* Note: all offsets are counted in 32-bit words and are relative
             * to the beginning of the current buffer. Offsets are set to -1 if
             * the corresponding data is not present/invalid. */

            /* The buffer pointer that was passed to singleStepInitState().
             * Will not be cleared on error or end of buffer. */
            DataBuffer *buffer = nullptr;

            /* Word offset of the last section header in the buffer. */
            s32 lastSectionHeaderOffset = -1;

            // points to the listfile subevent/module header preceding the module data
            std::array<s32, MaxVMEModules> lastModuleDataSectionHeaderOffsets;
            // first word offset of the module event data
            std::array<s32, MaxVMEModules> lastModuleDataBeginOffsets;
            // last word offset of the module event data
            std::array<s32, MaxVMEModules> lastModuleDataEndOffsets;

            enum StepResult: u8
            {
                StepResult_Unset,           // a non-event section was processed
                StepResult_EventHasMore,    // event section was processed but there's more subevents left
                StepResult_EventComplete,   // event section was processed and completed
                StepResult_AtEnd,           // the last input buffer was fully processed. Further calls to singleStepNextStep() are not allowed.
                StepResult_Error,           // an error occured during processing. Further calls to singleStepNextStep() are not allowed.
            };

            StepResult stepResult = StepResult_Unset;
        };

        ProcessingState singleStepInitState(DataBuffer *buffer);
        ProcessingState &singleStepNextStep(ProcessingState &procState);

        //
        // Additional data consumers
        //

        void attachDiagnostics(std::shared_ptr<MesytecDiagnostics> diag);
        void removeDiagnostics();
        bool hasDiagnostics() const;

        void attachModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer);
        void removeModuleConsumer(const std::shared_ptr<IStreamModuleConsumer> &consumer);
        void attachBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer);
        void removeBufferConsumer(const std::shared_ptr<IStreamBufferConsumer> &consumer);

    private:
        std::unique_ptr<MVMEStreamProcessorPrivate> m_d;
};

#endif /* __MVME_STREAM_PROCESSOR_H__ */
