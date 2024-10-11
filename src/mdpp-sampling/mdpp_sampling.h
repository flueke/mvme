#ifndef A477ED5F_CAA6_438E_98B6_000F69258CBC
#define A477ED5F_CAA6_438E_98B6_000F69258CBC

#include <QWidget>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>

#include "histo_ui.h"
#include "mdpp_decode.h"
#include "stream_processor_consumers.h"

class AnalysisServiceProvider;
using namespace std::chrono_literals;

namespace mesytec::mvme
{

class LIBMVME_EXPORT MdppSamplingConsumer: public QObject, public IStreamModuleConsumer
{
    Q_OBJECT
    signals:
        void moduleDataReady(const QUuid &moduleId, const std::vector<u32> &buffer, size_t linearEventNumber);

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

    public slots:
        void setTrace(const ChannelTrace *trace);
        const ChannelTrace *getTrace() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class LIBMVME_EXPORT GlTracePlotWidget : public QOpenGLWidget
{
    Q_OBJECT
    public:
        GlTracePlotWidget(QWidget *parent = nullptr) ;
        ~GlTracePlotWidget() override;

        //void setTrace(const ChannelTrace *trace);
        void setTrace(const float *samples, size_t size);

    protected:
        void initializeGL() override;
        void resizeGL(int w, int h) override;
        void paintGL() override;
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
        void updateUi();
        void replot() override;
        void handleModuleData(const QUuid &moduleId, const std::vector<u32> &buffer, size_t linearEventNumber);
        void addModuleInterest(const QUuid &moduleId);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

QVector<s16> interpolate(const QVector<s16> &samples, u32 factor);

}

#endif /* A477ED5F_CAA6_438E_98B6_000F69258CBC */
