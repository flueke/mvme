#ifndef __MVME_ANALYSIS_SERVICE_PROVIDER_H__
#define __MVME_ANALYSIS_SERVICE_PROVIDER_H__

#include "analysis/analysis.h"
#include "widget_registry.h"
#include "databuffer.h"
#include "listfile_replay.h"
#include "stream_worker_base.h"

struct AnalysisLoadFlags
{
    bool NoAutoResume: 1;
};

// Interface defining services used by the analysis GUI and analysis utilities
class LIBMVME_EXPORT AnalysisServiceProvider: public QObject
{
    Q_OBJECT
    signals:
        // VMEConfig changes
        void vmeConfigAboutToBeSet(VMEConfig *oldConfig, VMEConfig *newConfig);
        void vmeConfigChanged(VMEConfig *config);
        void vmeConfigFilenameChanged(const QString &fileName);

        // Forwarding VMEConfig signals
        void eventAdded(EventConfig *event);
        void eventAboutToBeRemoved(EventConfig *event);
        void moduleAdded(ModuleConfig *moduleConf);
        void moduleAboutToBeRemoved(ModuleConfig *moduleConf);

        /* Emitted when a new analysis is loaded.
         * Note that a nullptr may be passed in case loading did not succeed. */
        void analysisChanged(analysis::Analysis *analysis);

        /* Emitted when the current analysis file name changed. */
        void analysisConfigFileNameChanged(const QString &name);

        void analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op);

        // Various state changes...
        void mvmeStreamWorkerStateChanged(AnalysisWorkerState);
        void daqStateChanged(const DAQState &state);
        void modeChanged(GlobalMode mode);

        // VME Controller
        void vmeControllerAboutToBeChanged();
        void vmeControllerSet(VMEController *controller);

        // Workspace
        void workspaceDirectoryChanged(const QString &);

    public:
        AnalysisServiceProvider(QObject *parent = nullptr);
        ~AnalysisServiceProvider() override;

        // Workspace
        virtual QString getWorkspaceDirectory() = 0;

        // Retrieve the workspace path stored under the specified settingsKey.
        // Returns an empty string if no workspace is open. If no value is
        // stored under settingsKey then the optional defaultValue is returned.
        // If setIfDefaulted is true the defaultValue will be stored in the
        // workspace settings.
        virtual QString getWorkspacePath(
            const QString &settingsKey,
            const QString &defaultValue = QString(),
            bool setIfDefaulted = true) const = 0;

        virtual std::shared_ptr<QSettings> makeWorkspaceSettings() const = 0;

        // VMEConfig
        virtual VMEConfig *getVMEConfig() = 0;
        virtual QString getVMEConfigFilename() = 0;
        virtual void setVMEConfigFilename(const QString &filename) = 0;
        virtual void vmeConfigWasSaved() = 0;

        // Analysis
        virtual analysis::Analysis *getAnalysis() = 0;
        virtual QString getAnalysisConfigFilename() = 0;
        virtual void setAnalysisConfigFilename(const QString &filename) = 0;
        virtual void analysisWasSaved() = 0;
        virtual void analysisWasCleared() = 0;
        virtual void stopAnalysis() = 0;
        virtual void resumeAnalysis(analysis::Analysis::BeginRunOption runOption) = 0;
        virtual bool loadAnalysisConfig(const QString &filename) = 0;
        virtual bool loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo, AnalysisLoadFlags flags) = 0;

        // Widget registry
        virtual mesytec::mvme::WidgetRegistry *getWidgetRegistry() = 0;

        // Worker states
        virtual AnalysisWorkerState getAnalysisWorkerState() = 0;
        // TODO: get rid of the StreamWorkerBase dependency. Find something
        // smaller, that's easy to implement. Check usage in analysis_ui.cc,
        // etc.
        virtual StreamWorkerBase *getMVMEStreamWorker() = 0;

        virtual void logMessage(const QString &msg) = 0;


        virtual GlobalMode getGlobalMode() = 0; // DAQ or Listfile
        // TODO: make this Protected<ListfileReplayHandle> or return a copy
        virtual const ListfileReplayHandle &getReplayFileHandle() const = 0;


        virtual DAQStats getDAQStats() const = 0;
        virtual RunInfo getRunInfo() const = 0;

        virtual DAQState getDAQState() const = 0;

        virtual VMEController *getVMEController() = 0;

    public slots:
        virtual void addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op,
                                 s32 userLevel) = 0;
        virtual void setAnalysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op) = 0;

};

#endif /* __MVME_ANALYSIS_UI_SERVICE_PROVIDER_H__ */
