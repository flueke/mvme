#ifndef _MVME_LISTFILE_FILTERING_H_
#define _MVME_LISTFILE_FILTERING_H_

#include <memory>
#include <QUuid>
#include <QDialog>

#include "globals.h"
#include "stream_processor_consumers.h"
#include "stream_processor_counters.h"
#include "analysis/analysis_fwd.h"

// Note: to keep things simple this works for the MVLC only for now. Also the
// output is limited to zip and lz4 files. Support for other output formats,
// e.g.  zmq_ganil, could be added later.

struct ListfileFilterConfig
{
    // Ids of analysis condition operators used to filter the respective VME
    // event.
    std::vector<QUuid> filterConditionsByEvent;
    ListFileOutputInfo outputInfo;
};

class LIBMVME_EXPORT ListfileFilterStreamConsumer: public IStreamModuleConsumer
{
    public:
        ListfileFilterStreamConsumer();
        ~ListfileFilterStreamConsumer() override;

        void setLogger(Logger logger) override;
        Logger &getLogger() override;

        void startup() override {}
        void shutdown() override {}

        void beginRun(const RunInfo &runInfo,
                      const VMEConfig *vmeConfig,
                      const analysis::Analysis *analysis) override;

        void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

        void beginEvent(s32 eventIndex) override;
        void endEvent(s32 eventIndex) override;
        void processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size) override;
        void processModuleData(s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount) override;
        void processSystemEvent(s32 crateIndex, const u32 *header, u32 size) override;
        void processTimetick() override {}; // noop

        //void setEnabled(bool b) override;

        // Thread-safe, returns a copy of the counters.
        MVMEStreamProcessorCounters getCounters() const;

        // Not thread-safe. Needs to be called prior to starting a run.
        void setConfig(const ListfileFilterConfig &config);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class ListfileFilterDialog: public QDialog
{
    Q_OBJECT
    public:
        EventSettingsDialog(
            const VMEConfig *vmeConfig,
            const analaysis::Analysis* *analysis,
            QWidget *parent = nullptr);

        ~EventSettingsDialog() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif // _MVME_LISTFILE_FILTERING_H_
