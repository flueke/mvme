#include "listfile_replay.h"

#include "qt_util.h"
#include "util_zip.h"

ListfileReplayInfo open_listfile(const QString &filename)
{
    ListfileReplayInfo result;
    result.inputFilename = filename;

    // ZIP
    if (filename.toLower().endsWith(QSL(".zip")))
    {
        // IMPORTANT: Only one file from inside a ZIP archive can be open at
        // the same time. This is a limitation of the ZIP API.

        // Try reading the analysis config and the messages.log file from the
        // archive and store the contents in the result variables.
        // QuaZipFile creates an internal QuaZip instance and uses that to
        // unzip the file contents.
        {
            QuaZipFile f(filename, QSL("analysis.analysis"));
            if (f.open(QIODevice::ReadOnly))
                result.analysisBlob = f.readAll();
        }

        {
            QuaZipFile f(filename, QSL("messages.log"));
            if (f.open(QIODevice::ReadOnly))
                result.messages = f.readAll();
        }

        // Now open the archive directly and search for a listfile
        result.archive = std::make_unique<QuaZip>(filename);

        if (!result.archive->open(QuaZip::mdUnzip))
        {
            throw make_zip_error_string(
                "Could not open archive", result.archive.get());
        }

        QStringList fileNames = result.archive->getFileNameList();

        auto it = std::find_if(fileNames.begin(), fileNames.end(), [](const QString &str) {
            return (str.endsWith(QSL(".mvmelst"))
                    || str.endsWith(QSL(".mvlclst")));
        });

        if (it == fileNames.end())
            throw QString("No listfile found inside %1").arg(filename);

        result.listfileFilename = *it;

        Q_ASSERT(!result.listfileFilename.isEmpty());

        result.archive->setCurrentFile(result.listfileFilename);
        result.listfile = std::make_unique<QuaZipFile>(
            result.archive.get());

        if (!result.listfile->open(QIODevice::ReadOnly))
        {
            throw make_zip_error_string(
                "Could not open listfile",
                reinterpret_cast<QuaZipFile *>(result.listfile.get()));
        }
    }
    else
    {
        result.listfile = std::make_unique<QFile>(filename);

        if (!result.listfile->open(QIODevice::ReadOnly))
        {
            throw QString("Error opening %1 for reading: %2")
                .arg(filename)
                .arg(result.listfile->errorString());
        }
    }

    // TODO: try to figure out the type of the listfile: mvme or mvlc. Either
    // use the filename extension or (maybe better) read the magic bytes at the
    // start of the file. Then instantiate some type specific logic and try to
    // read the vme config from the listfile. If that fails there's something
    // wrong with the file.
    // XXX: Do this here or elsewhere?

    return result;

#if 0









        // try reading the VME config from inside the listfile
        auto json = result.listfile->getVMEConfigJSON();

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
#endif

}
