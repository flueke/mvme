#include "listfile_replay.h"

#include <array>
#include <cstring>
#include <QJsonDocument>

#include "qt_util.h"
#include "util_zip.h"
#include "mvme_listfile_utils.h"
#include "mvlc_listfile.h"
#include "vme_config_json_schema_updates.h"

namespace
{

ListfileBufferFormat detect_listfile_format(QIODevice *listfile)
{
    seek_in_file(listfile, 0);

    std::array<char, 8> buffer;

    // Note: older MVMELST files did not have a preamble at all, newer versions
    // do contain 'MVME' as a preamble so we default to MVMELST format.
    ListfileBufferFormat result = ListfileBufferFormat::MVMELST;

    ssize_t bytesRead = listfile->read(buffer.data(), buffer.size());

    if (std::strncmp(buffer.data(), "MVLC_ETH", bytesRead) == 0)
        result = ListfileBufferFormat::MVLC_ETH;
    else if (std::strncmp(buffer.data(), "MVLC_USB", bytesRead) == 0)
        result = ListfileBufferFormat::MVLC_USB;

    seek_in_file(listfile, 0);

    return result;
}

}

ListfileReplayHandle open_listfile(const QString &filename)
{
    ListfileReplayHandle result;
    result.inputFilename = filename;

    // ZIP
    if (filename.toLower().endsWith(QSL(".zip")))
    {
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

        // Now open the archive manually and search for a listfile
        result.archive = std::make_unique<QuaZip>(filename);

        if (!result.archive->open(QuaZip::mdUnzip))
        {
            throw make_zip_error_string(
                "Could not open archive", result.archive.get());
        }

        QStringList fileNames = result.archive->getFileNameList();

        auto it = std::find_if(fileNames.begin(), fileNames.end(), [](const QString &str) {
            return (str.endsWith(QSL(".mvmelst"))
                    || str.endsWith(QSL(".mvlclst"))
                    || str.endsWith(QSL(".mvlc_eth"))
                    || str.endsWith(QSL(".mvlc_usb"))
                    );
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

    assert(result.listfile);

    result.format = detect_listfile_format(result.listfile.get());

    return result;
}

std::pair<std::unique_ptr<VMEConfig>, std::error_code>
    read_vme_config_from_listfile(
        ListfileReplayHandle &handle,
        std::function<void (const QString &msg)> logger)
{
    switch (handle.format)
    {
        case ListfileBufferFormat::MVMELST:
            {
                ListFile lf(handle.listfile.get());
                lf.open();
                return read_config_from_listfile(&lf, logger);
            }

        case ListfileBufferFormat::MVLC_ETH:
        case ListfileBufferFormat::MVLC_USB:
            {
                auto json = QJsonDocument::fromJson(
                    mvlc_listfile::read_vme_config_data(*handle.listfile)).object();

                json = json.value("VMEConfig").toObject();
                json = mvme::vme_config::json_schema::convert_vmeconfig_to_current_version(json, logger);

                auto vmeConfig = std::make_unique<VMEConfig>();
                auto ec = vmeConfig->read(json);

                return std::pair<std::unique_ptr<VMEConfig>, std::error_code>(
                    std::move(vmeConfig), ec);
            } break;
    }

    return {};
}
