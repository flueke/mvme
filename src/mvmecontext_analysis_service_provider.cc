#include "mvmecontext_analysis_service_provider.h"
#include "mvme_context.h"
#include "mvme.h" // MVMEMainWindow

using namespace mesytec::mvme;

MVMEContextServiceProvider::MVMEContextServiceProvider(MVMEContext *ctx, QObject *parent)
    : AnalysisServiceProvider(parent)
    , ctx_(ctx)
{
}

QString MVMEContextServiceProvider::getWorkspaceDirectory()
{
    return ctx_->getWorkspaceDirectory();
}

QString MVMEContextServiceProvider::getWorkspacePath(const QString &settingsKey,
                                 const QString &defaultValue,
                                 bool setIfDefaulted) const
{
    return ctx_->getWorkspacePath(settingsKey, defaultValue, setIfDefaulted);
}

std::shared_ptr<QSettings> MVMEContextServiceProvider::makeWorkspaceSettings() const
{
    return ctx_->makeWorkspaceSettings();
}

VMEConfig *MVMEContextServiceProvider::getVMEConfig()
{
    return ctx_->getVMEConfig();
}

QString MVMEContextServiceProvider::getVMEConfigFilename()
{
    return ctx_->getVMEConfigFilename();
}

void MVMEContextServiceProvider::setVMEConfigFilename(const QString &filename)
{
    ctx_->setVMEConfigFilename(filename);
}

void MVMEContextServiceProvider::vmeConfigWasSaved()
{
    ctx_->vmeConfigWasSaved();
}


analysis::Analysis *MVMEContextServiceProvider::getAnalysis()
{
    return ctx_->getAnalysis();
}

QString MVMEContextServiceProvider::getAnalysisConfigFilename()
{
    return ctx_->getAnalysisConfigFilename();
}

void MVMEContextServiceProvider::setAnalysisConfigFilename(const QString &filename)
{
    ctx_->setAnalysisConfigFilename(filename);
}

void MVMEContextServiceProvider::analysisWasSaved()
{
    ctx_->analysisWasSaved();
}

void MVMEContextServiceProvider::analysisWasCleared()
{
    ctx_->analysisWasCleared();
}

void MVMEContextServiceProvider::stopAnalysis()
{
    ctx_->stopAnalysis();
}

void MVMEContextServiceProvider::resumeAnalysis(analysis::Analysis::BeginRunOption runOption)
{
    ctx_->resumeAnalysis(runOption);
}

bool MVMEContextServiceProvider::loadAnalysisConfig(const QString &filename)
{
    return ctx_->loadAnalysisConfig(filename);
}

bool MVMEContextServiceProvider::loadAnalysisConfig(const QJsonDocument &doc, const QString &inputInfo, AnalysisLoadFlags flags)
{
    return ctx_->loadAnalysisConfig(doc, inputInfo, flags);
}

mesytec::mvme::WidgetRegistry *MVMEContextServiceProvider::getWidgetRegistry()
{
    // TODO: move this into MVMEContext and get rid of the MVMEMainWindow dependency
    return ctx_->getMainWindow()->getWidgetRegistry();
}


AnalysisWorkerState MVMEContextServiceProvider::getAnalysisWorkerState()
{
    return ctx_->getMVMEStreamWorkerState();
}

StreamWorkerBase *MVMEContextServiceProvider::getMVMEStreamWorker()
{
    return ctx_->getMVMEStreamWorker();
}

void MVMEContextServiceProvider::logMessage(const QString &msg)
{
    ctx_->logMessage(msg);
}

GlobalMode MVMEContextServiceProvider::getGlobalMode()
{
    return ctx_->getMode();
}

const ListfileReplayHandle &MVMEContextServiceProvider::getReplayFileHandle() const
{
    return ctx_->getReplayFileHandle();
}


DAQStats MVMEContextServiceProvider::getDAQStats() const
{
    return ctx_->getDAQStats();
}

RunInfo MVMEContextServiceProvider::getRunInfo() const
{
    return ctx_->getRunInfo();
}

DAQState MVMEContextServiceProvider::getDAQState() const
{
    return ctx_->getDAQState();
}


void MVMEContextServiceProvider::addAnalysisOperator(QUuid eventId, const std::shared_ptr<analysis::OperatorInterface> &op,
                         s32 userLevel)
{
    ctx_->addAnalysisOperator(eventId, op, userLevel);
}

void MVMEContextServiceProvider::analysisOperatorEdited(const std::shared_ptr<analysis::OperatorInterface> &op)
{
    ctx_->analysisOperatorEdited(op);
}
