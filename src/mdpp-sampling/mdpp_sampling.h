#ifndef A477ED5F_CAA6_438E_98B6_000F69258CBC
#define A477ED5F_CAA6_438E_98B6_000F69258CBC

#include <QWidget>
#include "stream_processor_consumers.h"

class AnalysisServiceProvider;
using namespace std::chrono_literals;

namespace mesytec::mvme
{

static const auto MdppSamplePeriod = 12.5ns;

class LIBMVME_EXPORT MdppSamplingConsumer: public QObject, public IStreamModuleConsumer
{
    Q_OBJECT
    signals:
        void moduleDataReady(const QUuid &moduleId, const std::vector<u32> &buffer);

    public:
        explicit MdppSamplingConsumer(QObject *parent = nullptr);
        ~MdppSamplingConsumer() override;

        // IStreamModuleConsumer implementation
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

    public slots:
        void addModuleInterest(const QUuid &moduleId);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class LIBMVME_EXPORT MdppSamplingUi: public QWidget
{
    Q_OBJECT
    signals:
        void moduleInterestAdded(const QUuid &moduleId);

    public:
        MdppSamplingUi(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~MdppSamplingUi() override;

    public slots:
        //void handleModuleData(s32 crateIndex, s32 eventIndex, s32 moduleIndex, const std::vector<u32> buffer);
        void handleModuleData(const QUuid &moduleId, const std::vector<u32> &buffer);
        void addModuleInterest(const QUuid &moduleId);


    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* A477ED5F_CAA6_438E_98B6_000F69258CBC */
