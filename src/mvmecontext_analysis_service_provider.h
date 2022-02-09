#ifndef __MVMECONTEXT_ANALYSIS_SERVICE_PROVIDER_H__
#define __MVMECONTEXT_ANALYSIS_SERVICE_PROVIDER_H__

#include "analysis_service_provider.h"

// Implementation of the AnalysisServiceProvider using the "classic"
// MVMEContext to implement the functionality.

class MVMEContext;

class MVMEContextServiceProvider: public AnalysisServiceProvider
{
    Q_OBJECT
    public:
        MVMEContextServiceProvider(MVMEContext *ctx, QObject *parent = nullptr);

        // Workspace
        QString getWorkspaceDirectory() override;
        // Returns an empty string if no workspace is open. Otherwise returns the 
        QString getWorkspacePath(const QString &settingsKey,
                                         const QString &defaultValue = QString(),
                                         bool setIfDefaulted = true) const override;
        std::shared_ptr<QSettings> makeWorkspaceSettings() const override;


        // VMEConfig
        VMEConfig *getVMEConfig() override;
        QString getVMEConfigFilename() override;
        void setVMEConfigFilename(const QString &filename) override;
        void vmeConfigWasSaved() override;

        // Analysis
        analysis::Analysis *getAnalysis() override;
        QString getAnalysisConfigFilename() override;
        void setAnalysisConfigFilename(const QString &filename) override;
        void analysisWasSaved() override;
        void analysisWasCleared() override;
        void stopAnalysis() override;
        void resumeAnalysis(analysis::Analysis::BeginRunOption runOption) override;
        bool loadAnalysisConfig(const QString &filename) override;
        bool loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo, AnalysisLoadFlags flags) override;

        // Widget registry
        mesytec::mvme::WidgetRegistry *getWidgetRegistry() override;

        // Worker states
        AnalysisWorkerState getAnalysisWorkerState() override;
        StreamWorkerBase *getMVMEStreamWorker() override;

        void logMessage(const QString &msg) override;


        GlobalMode getGlobalMode() override; // DAQ or Listfile
        const ListfileReplayHandle &getReplayFileHandle() const override;

        DAQStats getDAQStats() const override;
        RunInfo getRunInfo() const override;

        DAQState getDAQState() const override;

    public slots:
        void addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op,
                                 s32 userLevel) override;
        void analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op) override;

    private:
        MVMEContext *ctx_;
};

#endif /* __MVMECONTEXT_ANALYSIS_SERVICE_PROVIDER_H__ */
