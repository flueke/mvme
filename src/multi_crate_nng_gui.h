#ifndef BA8142A2_50D3_441C_B32C_AED051A37E0D
#define BA8142A2_50D3_441C_B32C_AED051A37E0D

#include <optional>
#include "multi_crate_nng.h"

namespace mesytec::mvme::multi_crate
{

#if 0
struct StepViewModel
{
    QString name;
    bool isRunning = false;
    std::optional<LoopResult> lastResult;
    std::optional<SocketWorkPerformanceCounters> readerCounters;
    std::optional<SocketWorkPerformanceCounters> writerCounters;
};

struct CratePipelineViewModel
{
    QString name;
    bool isRunning = false;
    std::vector<StepViewModel> steps;
};

class CratePipelineViewModel2: public QObject
{
    Q_OBJECT
    signals:
        void refreshed();

    public:
        CratePipelineViewModel2(QObject *parent = nullptr);

    public slots:
        // Updates the view model with data fetched from the pipelines.
        void refresh();
};

struct CratePipelinesMonitorViewModel: public QObject
{
    Q_OBJECT
    signals:
        void refreshed();

    public:
        void addPipeline(const CratePipelineViewModel2 *pipelineViewModel);

    public slots:
        // Updates the view model with data fetched from the pipelines.
        void refresh();
};
#endif

class LIBMVME_EXPORT CratePipelineMonitorWidget: public QWidget
{
    Q_OBJECT
    public:
        CratePipelineMonitorWidget(QWidget* parent = nullptr);
        ~CratePipelineMonitorWidget() override;

        void setPipeline(const std::string &name, const CratePipeline &pipeline);
        void removePipeline(const std::string &name);

    public slots:
        // Connect this to the refreshed() signal of the view model. Refresh pipe
        // states and counters values.
        //void update();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

#if 0
class ReplayAppGui: public QWidget
{
    Q_OBJECT

    public:
        ReplayAppGui(QWidget* parent = nullptr);
        ~ReplayAppGui();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
#endif


}

#endif /* BA8142A2_50D3_441C_B32C_AED051A37E0D */
