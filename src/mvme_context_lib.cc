#include "mvme_context_lib.h"
#include "analysis/analysis.h"
#include "mvme_context.h"
#include "util_zip.h"

ContextOpenListfileResult context_open_listfile(MVMEContext *context, const QString &filename, u16 flags)
{
    ContextOpenListfileResult result = {};

    // Copy stuff over from the low level result.
    {
        auto lowLevelResult = open_listfile(filename);

        result.listfile = lowLevelResult.listfile.release();
        result.messages = lowLevelResult.messages;
        result.analysisBlob = lowLevelResult.analysisBlob;
        result.analysisFilename = lowLevelResult.analysisFilename;
    }

    // save current replay state and set new listfile on the context object
    bool wasReplaying = (context->getMode() == GlobalMode::ListFile
                         && context->getDAQState() == DAQState::Running);

    // Transfers ownership to the context.
    if (!context->setReplayFile(result.listfile))
    {
        result.listfile = nullptr;
        return result;
    }

    if (!result.analysisBlob.isEmpty())
    {
        context->setReplayFileAnalysisInfo(
            {
                filename,
                QSL("analysis.analysis"),
                result.analysisBlob
            });

        if (flags & OpenListfileFlags::LoadAnalysis)
        {
            context->loadAnalysisConfig(result.analysisBlob, QSL("ZIP Archive"));
            context->setAnalysisConfigFileName(QString());
        }
    }
    else
    {
        context->setReplayFileAnalysisInfo({});
    }

    if (wasReplaying)
    {
        context->startDAQReplay();
    }

    return result;
}

//
// AnalysisPauser
//
AnalysisPauser::AnalysisPauser(MVMEContext *context)
    : m_context(context)
    , m_prevState(m_context->getMVMEStreamWorkerState())
{
    qDebug() << __PRETTY_FUNCTION__ << "prevState =" << to_string(m_prevState);

    switch (m_prevState)
    {
        case MVMEStreamWorkerState::Running:
            m_context->stopAnalysis();
            break;

        case MVMEStreamWorkerState::Idle:
        case MVMEStreamWorkerState::Paused:
        case MVMEStreamWorkerState::SingleStepping:
            break;
    }
}

AnalysisPauser::~AnalysisPauser()
{
    qDebug() << __PRETTY_FUNCTION__ << "prevState =" << to_string(m_prevState);

    switch (m_prevState)
    {
        case MVMEStreamWorkerState::Running:
            m_context->resumeAnalysis(analysis::Analysis::KeepState);
            break;

        case MVMEStreamWorkerState::Idle:
        case MVMEStreamWorkerState::Paused:
        case MVMEStreamWorkerState::SingleStepping:
            {
                auto analysis = m_context->getAnalysis();

                if (analysis->anyObjectNeedsRebuild())
                {
                    qDebug() << __PRETTY_FUNCTION__
                        << "rebuilding analysis because at least one object needs a rebuild";
                    analysis->beginRun(analysis::Analysis::KeepState,
                                       [this] (const QString &msg) { m_context->logMessage(msg); });

                }
            }
            break;
    }
}
