#ifndef _MVME_LISTFILE_FILTERING_H_
#define _MVME_LISTFILE_FILTERING_H_

#include <memory>
#include <QUuid>
#include <QDialog>
#include <QMap>

#include "globals.h"
#include "stream_processor_consumers.h"
#include "stream_processor_counters.h"

// Note: to keep things simple this works for the MVLC only for now. Also the
// output is limited to zip and lz4 files. Support for other output formats,
// e.g.  zmq_ganil, could be added later.

// Config for the listfile filter module. Stored in the Analysis object passed
// to ListfileFilterStreamConsumer::beginRun().
struct ListfileFilterConfig
{
    struct FilterEventEntry
    {
        QUuid eventId;      // The vme config event to be filtered.
        QUuid conditionId;  // The analysis condition to use for filtering.
    };

    // Ids of analysis condition operators used to filter the respective VME
    // event.
    // VME event id -> analysis condition id
    QMap<QUuid, QUuid> eventEntries;

    // Configuration for the output listfile to be written.
    ListFileOutputInfo outputInfo;

    bool enabled = false;
};

QVariant listfile_filter_config_to_variant(const ListfileFilterConfig &cfg);
ListfileFilterConfig listfile_filter_config_from_variant(const QVariant &var);

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
                      analysis::Analysis *analysis) override;

        void endRun(const DAQStats &stats, const std::exception *e = nullptr) override;

        void beginEvent(s32 eventIndex) override;
        void endEvent(s32 eventIndex) override;
        void processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size) override;
        void processModuleData(s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount) override;
        void processSystemEvent(s32 crateIndex, const u32 *header, u32 size) override;
        void processTimetick() override {}; // noop

        void setRunNotes(const QString &runNotes);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class AnalysisServiceProvider;

class ListfileFilterDialog: public QDialog
{
    Q_OBJECT
    public:
        ListfileFilterDialog(
            AnalysisServiceProvider *asp,
            QWidget *parent = nullptr);

        ~ListfileFilterDialog() override;

        void accept() override;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#endif // _MVME_LISTFILE_FILTERING_H_
