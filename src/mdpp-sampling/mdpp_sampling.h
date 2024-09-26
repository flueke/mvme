#ifndef A477ED5F_CAA6_438E_98B6_000F69258CBC
#define A477ED5F_CAA6_438E_98B6_000F69258CBC

#include <QWidget>

#include "histo_ui.h"
#include "stream_processor_consumers.h"
#include "util.h"

class AnalysisServiceProvider;
using namespace std::chrono_literals;

namespace mesytec::mvme
{

static const auto MdppSamplePeriod = 12.5ns;

struct LIBMVME_EXPORT ChannelTrace
{
    // linear event number incremented on each event from the source module
    size_t eventNumber = 0;
    QUuid moduleId;
    s32 channel = -1;
    float amplitude = make_quiet_nan(); // extracted amplitude value
    float time = make_quiet_nan(); // extracted time value
    u32 amplitudeData = 0; // raw amplitude data word
    u32 timeData = 0; // raw time data word
    QVector<s16> samples; // samples are 14 bit signed, converted to and stored as 16 bit signed
};

// Clear the sample memory and reset all other fields to default values.
void reset_trace(ChannelTrace &trace);

// Can hold traces from multiple channels or alternatively the traces list can be
// used to store a history of traces for a particular channel.
struct LIBMVME_EXPORT DecodedMdppSampleEvent
{
    // Set to the linear event number when decoding data from an mdpp. leave set
    // to -1 when using this structure as a history buffer for a single channel.
    ssize_t eventNumber = -1;
    QUuid moduleId;
    u32 header = 0;
    u64 timestamp = 0;
    QList<ChannelTrace> traces;
    u8 headerModuleId = 0; // extracted from the header word for convenient access
};

DecodedMdppSampleEvent LIBMVME_EXPORT decode_mdpp_samples(const u32 *data, const size_t size);

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

class LIBMVME_EXPORT TracePlotWidget: public histo_ui::PlotWidget
{
    Q_OBJECT
    public:
        TracePlotWidget(QWidget *parent = nullptr);
        ~TracePlotWidget() override;

        void setTrace(const ChannelTrace *trace);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class LIBMVME_EXPORT MdppSamplingUi: public histo_ui::IPlotWidget
{
    Q_OBJECT
    signals:
        void moduleInterestAdded(const QUuid &moduleId);

    public:
        MdppSamplingUi(AnalysisServiceProvider *asp, QWidget *parent = nullptr);
        ~MdppSamplingUi() override;

        QwtPlot *getPlot() override;
        const QwtPlot *getPlot() const override;
        QToolBar *getToolBar() override;
        QStatusBar *getStatusBar() override;

    public slots:
        void replot() override;
        void handleModuleData(const QUuid &moduleId, const std::vector<u32> &buffer);
        void addModuleInterest(const QUuid &moduleId);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

}

#endif /* A477ED5F_CAA6_438E_98B6_000F69258CBC */
