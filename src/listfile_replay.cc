/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "listfile_replay.h"

#include <array>
#include <cstring>
#include <QJsonDocument>
#include <mesytec-mvlc/mesytec-mvlc.h>

#include "qt_util.h"
#include "util_zip.h"
#include "mvme_listfile_utils.h"
#include "mvme_mvlc_listfile.h"
#include "vme_config_json_schema_updates.h"

using namespace mesytec;

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

    // ZIP archive
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

        {
            QuaZipFile f(filename, QSL("mvme_run_notes.txt"));
            if (f.open(QIODevice::ReadOnly))
                result.runNotes = QString::fromLocal8Bit(f.readAll());
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
                    || str.endsWith(QSL(".mvlclst.lz4"))
                    || str.endsWith(QSL(".mvlc_eth"))
                    || str.endsWith(QSL(".mvlc_usb"))
                    );
        });

        if (it == fileNames.end())
            throw QString("No listfile found inside %1").arg(filename);

        result.listfileFilename = *it;
        auto lfName  = *it;

        // Check whether we're dealing with an MVLC listfile or with a classic
        // mvmelst listfile.
        if (lfName.endsWith(QSL(".mvlclst"))
            || lfName.endsWith(QSL(".mvlclst.lz4"))
            || lfName.endsWith(QSL(".mvlc_eth"))
            || lfName.endsWith(QSL(".mvlc_usb")))
        {
            try
            {
                mvlc::listfile::ZipReader zipReader;
                zipReader.openArchive(filename.toStdString());
                auto lfh = zipReader.openEntry(lfName.toStdString());
                auto preamble = mvlc::listfile::read_preamble(*lfh);
                if (preamble.magic == mvlc::listfile::get_filemagic_eth())
                    result.format = ListfileBufferFormat::MVLC_ETH;
                else if (preamble.magic == mvlc::listfile::get_filemagic_usb())
                    result.format = ListfileBufferFormat::MVLC_USB;
                else
                    throw QString("Unknown listfile format (file=%1, listfile=%2")
                        .arg(filename).arg(lfName);
            }
            catch (const std::runtime_error &e)
            {
                throw QString("Error detecting listfile format (file=%1, error=%2")
                    .arg(filename).arg(e.what());
            }
        }
        else // mvmelst file
        {
            result.archive->setCurrentFile(result.listfileFilename);

            {
                auto zipFile = std::make_unique<QuaZipFile>(result.archive.get());
                zipFile->setFileName(result.listfileFilename);
                result.listfile = std::move(zipFile);
            }

            if (!result.listfile->open(QIODevice::ReadOnly))
            {
                throw make_zip_error_string(
                    "Could not open listfile",
                    reinterpret_cast<QuaZipFile *>(result.listfile.get()));
            }

            assert(result.listfile);
            result.format = detect_listfile_format(result.listfile.get());
        }
    }
    else // flat file
    {
        result.listfile = std::make_unique<QFile>(filename);

        if (!result.listfile->open(QIODevice::ReadOnly))
        {
            throw QString("Error opening %1 for reading: %2")
                .arg(filename)
                .arg(result.listfile->errorString());
        }

        assert(result.listfile);
        result.format = detect_listfile_format(result.listfile.get());
    }

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
                mvlc::listfile::ZipReader zipReader;
                zipReader.openArchive(handle.inputFilename.toStdString());
                auto lfh = zipReader.openEntry(handle.listfileFilename.toStdString());
                auto preamble = mvlc::listfile::read_preamble(*lfh);

                for (const auto &sysEvent: preamble.systemEvents)
                {
                    qDebug() << __PRETTY_FUNCTION__ << "found preamble sysEvent type"
                        << mvlc::system_event_type_to_string(sysEvent.type).c_str();
                }

                auto it = std::find_if(
                    std::begin(preamble.systemEvents),
                    std::end(preamble.systemEvents),
                    [] (const mvlc::listfile::SystemEvent &sysEvent)
                    {
                        return sysEvent.type == mvlc::system_event::subtype::MVMEConfig;
                    });

                // TODO: implement a fallback using the MVLCCrateConfig data
                // (or make it an explicit action in the GUI and put the code
                // elsewhere)
                if (it == std::end(preamble.systemEvents))
                    throw std::runtime_error("No MVMEConfig found in listfile");

                qDebug() << __PRETTY_FUNCTION__ << "found MVMEConfig in listfile preamble, size =" << it->contents.size();

                QByteArray qbytes(
                    reinterpret_cast<const char *>(it->contents.data()),
                    it->contents.size());

                auto doc = QJsonDocument::fromJson(qbytes);
                auto json = doc.object();
                json = json.value("VMEConfig").toObject();

                mvme::vme_config::json_schema::SchemaUpdateOptions updateOptions;
                updateOptions.skip_v4_VMEScriptVariableUpdate = true;

                json = mvme::vme_config::json_schema::convert_vmeconfig_to_current_version(json, logger, updateOptions);
                auto vmeConfig = std::make_unique<VMEConfig>();
                auto ec = vmeConfig->read(json);
                return std::pair<std::unique_ptr<VMEConfig>, std::error_code>(
                    std::move(vmeConfig), ec);
            }
            break;
    }

    return {};
}
