#include "mvme_context_lib.h"
#include "mvme_context.h"
#include "util_zip.h"

OpenListfileResultLowLevel open_listfile(const QString &filename)
{
    OpenListfileResultLowLevel result;

    if (filename.isEmpty())
        return result;

    // ZIP
    if (filename.toLower().endsWith(QSL(".zip")))
    {
        QString listfileFileName;

        // find and use the first .mvmelst file inside the archive
        {
            QuaZip archive(filename);

            if (!archive.open(QuaZip::mdUnzip))
            {
                throw make_zip_error("Could not open archive", &archive);
            }

            QStringList fileNames = archive.getFileNameList();

            auto it = std::find_if(fileNames.begin(), fileNames.end(), [](const QString &str) {
                return str.endsWith(QSL(".mvmelst"));
            });

            if (it == fileNames.end())
            {
                throw QString("No listfile found inside %1").arg(filename);
            }

            listfileFileName = *it;
        }

        Q_ASSERT(!listfileFileName.isEmpty());

        auto inFile = std::make_unique<QuaZipFile>(filename, listfileFileName);

        if (!inFile->open(QIODevice::ReadOnly))
        {
            throw make_zip_error("Could not open listfile", inFile.get());
        }

        result.listfile = std::make_unique<ListFile>(inFile.release()); 

        if (!result.listfile->open())
        {
            throw QString("Error opening listfile inside %1 for reading").arg(filename);
        }

        // try reading the VME config from inside the listfile
        auto json = result.listfile->getDAQConfig();

        if (json.isEmpty())
        {
            throw QString("Listfile does not contain a valid VME configuration");
        }

        /* Check if there's an analysis file inside the zip archive, read it,
         * store contents in state and decide on whether to directly load it.
         * */
        {
            QuaZipFile inFile(filename, QSL("analysis.analysis"));

            if (inFile.open(QIODevice::ReadOnly))
            {
                result.analysisBlob = inFile.readAll();
                result.analysisFilename = QSL("analysis.analysis");
            }
        }

        // Try to read the logfile from the archive
        {
            QuaZipFile inFile(filename, QSL("messages.log"));

            if (inFile.open(QIODevice::ReadOnly))
            {
                result.messages = inFile.readAll();
            }
        }
    }
    // Plain
    else
    {
        result.listfile = std::make_unique<ListFile>(filename);

        if (!result.listfile->open())
        {
            throw QString("Error opening %1 for reading").arg(filename);
        }

        auto json = result.listfile->getDAQConfig();

        if (json.isEmpty())
        {
            throw QString("Listfile does not contain a valid VME configuration");
        }
    }

    return result;
}

OpenListfileResult open_listfile(MVMEContext *context, const QString &filename, u16 flags)
{
    OpenListfileResult result = {};

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
        context->startReplay();
    }

    return result;
}
