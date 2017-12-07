#ifndef __MVME_STREAM_UTIL_H__
#define __MVME_STREAM_UTIL_H__

#include <cassert>
#include <stdexcept>

#include "databuffer.h"
#include "mvme_listfile.h"

struct MVMEStreamWriterError: public std::runtime_error
{
    MVMEStreamWriterError(const char *s) : std::runtime_error(s) {}
};

struct MVMEStreamWriterLogicError: public MVMEStreamWriterError
{
    MVMEStreamWriterLogicError(const char *s) : MVMEStreamWriterError(s) {}
};

struct MVMEStreamWriterSizeError: public MVMEStreamWriterError
{
    MVMEStreamWriterSizeError(const char *s) : MVMEStreamWriterError(s) {}
};

class MVMEStreamWriterHelper
{
    public:
        using LF = listfile_v1;

        MVMEStreamWriterHelper(DataBuffer *outputBuffer)
            : m_outputBuffer(outputBuffer)
            , m_eventSize(0)
            , m_moduleSize(0)
            , m_eventHeaderOffset(-1)
            , m_moduleHeaderOffset(-1)
        {
            assert(outputBuffer);
        }

        inline u32 *openEventSection(int eventIndex)
        {
            if (hasOpenEventSection())
                throw MVMEStreamWriterLogicError("Cannot open new event section while event section is open");

            if (hasOpenModuleSection())
                throw MVMEStreamWriterLogicError("Cannot open new event section while module section is open");

            m_eventHeaderOffset = m_outputBuffer->used;
            u32 *eventHeader = m_outputBuffer->asU32();
            m_outputBuffer->used += sizeof(u32);
            m_eventSize = 0;

            *eventHeader = ((ListfileSections::SectionType_Event << LF::SectionTypeShift) & LF::SectionTypeMask)
                | ((eventIndex << LF::EventTypeShift) & LF::EventTypeMask);

            return eventHeader;
        }

        inline u32 closeEventSection()
        {
            if (!hasOpenEventSection())
                throw MVMEStreamWriterLogicError("Cannot close event section: no event section open");

            if (hasOpenModuleSection())
                throw MVMEStreamWriterLogicError("Cannot close event section: module section is open");

            u32 *eventHeader = m_outputBuffer->asU32(m_eventHeaderOffset);
            *eventHeader |= (m_eventSize << LF::SectionSizeShift) & LF::SectionSizeMask;
            m_eventHeaderOffset = -1;
            return m_eventSize;
        }

        inline u32 *openModuleSection(u32 moduleType)
        {
            if (!hasOpenEventSection())
                throw MVMEStreamWriterLogicError("Cannot open module section without open event section");

            if (hasOpenModuleSection())
                throw MVMEStreamWriterLogicError("Cannot open new module section while module section is open");

            if (m_eventSize >= LF::SectionMaxWords)
                throw MVMEStreamWriterSizeError("Cannot open module section: event section size exceeded");

            m_moduleHeaderOffset = m_outputBuffer->used;
            u32 *moduleHeader = m_outputBuffer->asU32();
            m_outputBuffer->used += sizeof(u32);
            m_eventSize++;
            m_moduleSize = 0;

            *moduleHeader = (moduleType << LF::ModuleTypeShift) & LF::ModuleTypeMask;

            return moduleHeader;
        }

        inline u32 closeModuleSection()
        {
            if (!hasOpenEventSection())
                throw MVMEStreamWriterLogicError("Cannot close module section: no event section open");

            if (!hasOpenModuleSection())
                throw MVMEStreamWriterLogicError("Cannot close module section: no module section open");

            u32 *moduleHeader = m_outputBuffer->asU32(m_moduleHeaderOffset);
            *moduleHeader |= (m_moduleSize << LF::SubEventSizeShift) & LF::SubEventSizeMask;
            m_moduleHeaderOffset = -1;
            return m_moduleSize;
        }

        inline void writeEventData(u32 dataWord)
        {
            if (!hasOpenEventSection())
                throw MVMEStreamWriterLogicError("Cannot write event data: no event section open");

            if (hasOpenModuleSection())
                throw MVMEStreamWriterLogicError("Cannot write event data: a module section open");

            if (m_eventSize >= LF::SectionMaxWords)
                throw MVMEStreamWriterSizeError("Cannot write event data: event section size exceeded");

            *m_outputBuffer->asU32() = dataWord;
            m_outputBuffer->used += sizeof(u32);
            m_eventSize++;
        }

        inline void writeModuleData(u32 dataWord)
        {
            if (!hasOpenEventSection())
                throw MVMEStreamWriterLogicError("Cannot write module data: no event section open");

            if (!hasOpenModuleSection())
                throw MVMEStreamWriterLogicError("Cannot write module data: no module section open");

            if (m_eventSize >= LF::SectionMaxWords)
                throw MVMEStreamWriterSizeError("Cannot write module data: event section size exceeded");

            if (m_moduleSize >= LF::SubEventMaxWords)
                throw MVMEStreamWriterSizeError("Cannot write module data: module section size exceeded");

            *m_outputBuffer->asU32() = dataWord;
            m_outputBuffer->used += sizeof(u32);
            m_eventSize++;
            m_moduleSize++;
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


#endif /* __MVME_STREAM_UTIL_H__ */
