#include "mvme_context_lib.h"
#include "mvme_context.h"
#include "util_zip.h"

OpenListfileResult open_listfile(MVMEContext *context, const QString &filename, u16 flags)
{
    OpenListfileResult result = {};

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

        auto listFile = std::make_unique<ListFile>(inFile.release());

        if (!listFile->open())
        {
            throw QString("Error opening listfile inside %1 for reading").arg(filename);
        }

        // try reading the VME config from inside the listfile
        auto json = listFile->getDAQConfig();

        if (json.isEmpty())
        {
            throw QString("Listfile does not contain a valid VME configuration");
        }

        // save current replay state and set new listfile on the context object
        bool wasReplaying = (context->getMode() == GlobalMode::ListFile
                             && context->getDAQState() == DAQState::Running);

        if (!context->setReplayFile(listFile.release()))
        {
            return result;
        }

        result.listfile = context->getReplayFile();

        /* Check if there's an analysis file inside the zip archive, read it,
         * store contents in state and decide on whether to directly load it.
         * */
        {
            QuaZipFile inFile(filename, QSL("analysis.analysis"));

            if (inFile.open(QIODevice::ReadOnly))
            {
                result.analysisBlob = inFile.readAll();
                result.analysisFilename = QSL("analysis.analysis");

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
        }

        // Try to read the logfile from the archive
        {
            QuaZipFile inFile(filename, QSL("messages.log"));

            if (inFile.open(QIODevice::ReadOnly))
            {
                result.messages = inFile.readAll();
            }
        }

        if (wasReplaying)
        {
            context->startReplay();
        }
    }
    // Plain
    else
    {
        auto listFile = std::make_unique<ListFile>(filename);

        if (!listFile->open())
        {
            throw QString("Error opening %1 for reading").arg(filename);
        }

        auto json = listFile->getDAQConfig();

        if (json.isEmpty())
        {
            throw QString("Listfile does not contain a valid VME configuration");
        }

        bool wasReplaying = (context->getMode() == GlobalMode::ListFile
                             && context->getDAQState() == DAQState::Running);

        if (!context->setReplayFile(listFile.release()))
        {
            return result;
        }

        result.listfile = context->getReplayFile();

        if (wasReplaying)
        {
            context->startReplay();
        }
    }

    return result;
}
