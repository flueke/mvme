#include "mvme_context_lib.h"
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
    : context(context)
{
    was_running = context->isAnalysisRunning();

    qDebug() << __PRETTY_FUNCTION__ << "was_running =" << was_running;

    if (was_running)
    {
        context->stopAnalysis();
    }
}

AnalysisPauser::~AnalysisPauser()
{
    qDebug() << __PRETTY_FUNCTION__ << "was_running =" << was_running;
    if (was_running)
    {
        context->resumeAnalysis();
    }
}
