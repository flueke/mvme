#ifndef __MVME_ROOT_STREAM_CONSUMERS_H__
#define __MVME_ROOT_STREAM_CONSUMERS_H__

#include "libmvme_export.h"
#include "../mvme_stream_processor.h"

#include <QProcess>

namespace mvme_root
{

class LIBMVME_EXPORT AnalysisDataWriter: public QObject, public IMVMEStreamModuleConsumer
{
    public:
        AnalysisDataWriter(QObject *parent = nullptr);
        virtual ~AnalysisDataWriter();

        virtual void beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig, const analysis::Analysis *analysis, Logger logger) override;
        virtual void endRun(const std::exception *e = nullptr) override;

        virtual void beginEvent(s32 eventIndex) override;
        virtual void endEvent(s32 eventIndex) override;
        virtual void processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size) override;
        virtual void processTimetick() override;

    private:
        QProcess *m_writerProcess = nullptr;
        QDataStream m_writerOut;

        Logger m_logger;
};

} // end namespace mvme_root

#endif /* __MVME_ROOT_STREAM_CONSUMERS_H__ */
