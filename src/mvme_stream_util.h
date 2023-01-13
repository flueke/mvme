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
#ifndef __MVME_STREAM_UTIL_H__
#define __MVME_STREAM_UTIL_H__

#include <cassert>
#include <stdexcept>

#include "libmvme_export.h"
#include "databuffer.h"
#include "mvme_listfile.h"
#include "mvme_stream_iter.h"

/* Utility class to be used by readout workers to ease and unify listfile
 * generation. */
class LIBMVME_EXPORT MVMEStreamWriterHelper
{
    public:
        enum Flags
        {
            ResultOk,
            EventSizeExceeded,
            ModuleSizeExceeded,
            NestingError
        };

        struct CloseResult
        {
            Flags flags = ResultOk;
            u32 sectionBytes = 0;
        };

        explicit MVMEStreamWriterHelper(DataBuffer *outputBuffer = nullptr)
            : m_outputBuffer(outputBuffer)
            , m_eventSize(0)
            , m_moduleSize(0)
            , m_eventHeaderOffset(-1)
            , m_moduleHeaderOffset(-1)
        { }

        void setOutputBuffer(DataBuffer *outputBuffer)
        {
            m_outputBuffer = outputBuffer;
        }

        DataBuffer *outputBuffer() const { return m_outputBuffer; }

        inline u32 eventSize() const { return m_eventSize; }
        inline u32 moduleSize() const { return m_moduleSize; }
        inline s32 eventHeaderOffset() const { return m_eventHeaderOffset; }
        inline s32 moduleHeaderOffset() const { return m_moduleHeaderOffset; }

        inline Flags openEventSection(int eventIndex)
        {
            assert(m_outputBuffer);

            if (hasOpenEventSection() || hasOpenModuleSection())
                return NestingError;

            m_eventHeaderOffset = m_outputBuffer->used;
            u32 *eventHeader = m_outputBuffer->asU32();
            m_outputBuffer->used += sizeof(u32);
            m_eventSize = 0;

            const auto &lfc = listfile_constants();

            *eventHeader = ((ListfileSections::SectionType_Event << lfc.SectionTypeShift) & lfc.SectionTypeMask)
                | ((eventIndex << lfc.EventIndexShift) & lfc.EventIndexMask);

            return ResultOk;
        }

        inline CloseResult closeEventSection()
        {
            assert(m_outputBuffer);

            if (!hasOpenEventSection() || hasOpenModuleSection())
                return { NestingError, 0 };

            const auto &lfc = listfile_constants();

            u32 *eventHeader = m_outputBuffer->asU32(m_eventHeaderOffset);
            *eventHeader |= (m_eventSize << lfc.SectionSizeShift) & lfc.SectionSizeMask;
            m_eventHeaderOffset = -1;

            return { ResultOk, static_cast<u32>(m_eventSize * sizeof(u32)) };
        }

        inline Flags openModuleSection(u32 moduleType)
        {
            assert(m_outputBuffer);

            if (!hasOpenEventSection() || hasOpenModuleSection())
                return NestingError;

            const auto &lfc = listfile_constants();

            if (m_eventSize >= lfc.SectionMaxWords)
                return EventSizeExceeded;

            m_moduleHeaderOffset = m_outputBuffer->used;
            u32 *moduleHeader = m_outputBuffer->asU32();
            m_outputBuffer->used += sizeof(u32);
            m_eventSize++;
            m_moduleSize = 0;

            *moduleHeader = (moduleType << lfc.ModuleTypeShift) & lfc.ModuleTypeMask;

            return ResultOk;
        }

        inline CloseResult closeModuleSection()
        {
            assert(m_outputBuffer);

            if (!hasOpenEventSection() || !hasOpenModuleSection())
                return { NestingError, 0 };

            const auto &lfc = listfile_constants();

            u32 *moduleHeader = m_outputBuffer->asU32(m_moduleHeaderOffset);
            *moduleHeader |= (m_moduleSize << lfc.ModuleDataSizeShift) & lfc.ModuleDataSizeMask;
            m_moduleHeaderOffset = -1;

            return { ResultOk, static_cast<u32>(m_moduleSize * sizeof(u32)) };
        }

        inline Flags writeEventData(u32 dataWord)
        {
            assert(m_outputBuffer);

            if (!hasOpenEventSection() || hasOpenModuleSection())
                return NestingError;

            const auto &lfc = listfile_constants();

            if (m_eventSize >= lfc.SectionMaxWords)
                return EventSizeExceeded;

            *m_outputBuffer->asU32() = dataWord;
            m_outputBuffer->used += sizeof(u32);
            m_eventSize++;

            return ResultOk;
        }

        inline Flags writeModuleData(u32 dataWord)
        {
            assert(m_outputBuffer);

            if (!hasOpenEventSection() || !hasOpenModuleSection())
                return NestingError;

            const auto &lfc = listfile_constants();

            if (m_eventSize >= lfc.SectionMaxWords)
                return EventSizeExceeded;

            if (m_moduleSize >= lfc.ModuleDataMaxWords)
                return ModuleSizeExceeded;

            *m_outputBuffer->asU32() = dataWord;
            m_outputBuffer->used += sizeof(u32);
            m_eventSize++;
            m_moduleSize++;

            return ResultOk;
        }

        inline bool hasOpenEventSection() const { return m_eventHeaderOffset >= 0; }
        inline bool hasOpenModuleSection() const { return m_moduleHeaderOffset >= 0; }

    private:
        DataBuffer *m_outputBuffer;
        u32 m_eventSize;
        u32 m_moduleSize;
        s32 m_eventHeaderOffset;
        s32 m_moduleHeaderOffset;
};

LIBMVME_EXPORT mvme_stream::StreamInfo streaminfo_from_vmeconfig(VMEConfig *vmeConfig, u32 listfileVersion = CurrentListfileVersion);
LIBMVME_EXPORT mvme_stream::StreamInfo streaminfo_from_listfile(ListFile *listfile);

#endif /* __MVME_STREAM_UTIL_H__ */
