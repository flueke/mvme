/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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

#include "vme_daq.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "mvme_listfile_utils.h"
#include "util_zip.h"
#include "vme_config_scripts.h"

using namespace mesytec::mvme;

//
// vme_daq_init
//
QVector<ScriptWithResult>
vme_daq_init(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    vme_script::run_script_options::Flag opts
    )
{
    using namespace vme_script::run_script_options;

    QVector<ScriptWithResult> ret;

    auto startScripts = config->getGlobalObjectRoot().findChild<ContainerObject *>(
        "daq_start")->findChildren<VMEScriptConfig *>();

    if (!startScripts.isEmpty())
    {
        logger(QSL(""));
        logger(QSL("Global DAQ Start scripts:"));
        for (auto scriptConfig: startScripts)
        {
            if (!scriptConfig->isEnabled())
                continue;

            logger(QString("  %1").arg(scriptConfig->objectName()));
            auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };

            auto script = parse(scriptConfig);
            auto results = run_script(controller, script, indentingLogger,
                opts | LogEachResult);

            ret.push_back({ scriptConfig, results });
            if ((opts & AbortOnError) && has_errors(results))
                return ret;
        }
    }

    logger(QSL(""));
    logger(QSL("Initializing Modules:"));
    for (auto eventConfig: config->getEventConfigs())
    {
        for (auto module: eventConfig->getModuleConfigs())
        {
            if (!module->isEnabled())
            {
                logger(QString("  %1.%2: Disabled in VME configuration")
                           .arg(eventConfig->objectName())
                           .arg(module->objectName())
                          );
                continue;
            }

            logger(QString("  %1.%2")
                       .arg(eventConfig->objectName())
                       .arg(module->objectName())
                      );

            QVector<VMEScriptConfig *> scripts;
            scripts.push_back(module->getResetScript());
            scripts.append(module->getInitScripts());

            for (auto scriptConfig: scripts)
            {
                logger(QSL("    %1").arg(scriptConfig->objectName()));
                auto indentingLogger = [logger](const QString &str) { logger(QSL("      ") + str); };

                auto script = parse(scriptConfig, module->getBaseAddress());
                auto results = run_script(
                    controller, script,
                    indentingLogger, opts | LogEachResult);
                ret.push_back({ scriptConfig, results });
                if ((opts & AbortOnError) && has_errors(results))
                    return ret;
            }
        }
    }

    logger(QSL("Events DAQ Start"));
    for (auto eventConfig: config->getEventConfigs())
    {
        auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
        auto scriptConfig = eventConfig->vmeScripts["daq_start"];
        auto script = parse(scriptConfig);

        if (!script.isEmpty())
            logger(QString("  %1").arg(eventConfig->objectName()));

        auto results = run_script(controller, script, indentingLogger, opts | LogEachResult);
        ret.push_back({ scriptConfig, results });
        if ((opts & AbortOnError) && has_errors(results))
            return ret;
    }

    return ret;
}

//
// vme_daq_shutdown
//
QVector<ScriptWithResult>
vme_daq_shutdown(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    vme_script::run_script_options::Flag opts
    )
{
    using namespace vme_script::run_script_options;

    QVector<ScriptWithResult> ret;

    logger(QSL("Events DAQ Stop"));
    for (auto eventConfig: config->getEventConfigs())
    {
        logger(QString("  %1").arg(eventConfig->objectName()));
        auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
        auto scriptConfig = eventConfig->vmeScripts["daq_stop"];
        auto script = parse(scriptConfig);
        auto results = run_script(controller, script, indentingLogger,
                                  opts | LogEachResult);
        ret.push_back({ scriptConfig, results });
        if ((opts & AbortOnError) && has_errors(results))
            return ret;
    }

    auto stopScripts = config->getGlobalObjectRoot().findChild<ContainerObject *>(
        "daq_stop")->findChildren<VMEScriptConfig *>();

    if (!stopScripts.isEmpty())
    {
        logger(QSL("Global DAQ Stop scripts:"));
        for (auto scriptConfig: stopScripts)
        {
            if (!scriptConfig->isEnabled())
                continue;

            logger(QString("  %1").arg(scriptConfig->objectName()));
            auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
            auto script = parse(scriptConfig);
            auto results = run_script(controller, script, indentingLogger,
                                      opts | LogEachResult);
            ret.push_back({ scriptConfig, results });
            if ((opts & AbortOnError) && has_errors(results))
                return ret;
        }
    }

    return ret;
}

//
// build_event_readout_script
//
vme_script::VMEScript build_event_readout_script(
    EventConfig *eventConfig,
    u8 flags)
{
    using namespace vme_script;

    VMEScript result;

    result += parse(eventConfig->vmeScripts["readout_start"]);

    for (auto module: eventConfig->getModuleConfigs())
    {
        if (module->isEnabled())
        {
            result += parse(module->getReadoutScript(), module->getBaseAddress());
        }

        /* If the module is disabled only the EndMarker will be present in the
         * readout data. This looks the same as if the module readout did not
         * yield any data at all. */
        if (!(flags & EventReadoutBuildFlags::NoModuleEndMarker))
        {
            Command marker;
            marker.type = CommandType::Marker;
            marker.value = EndMarker;
            result += marker;
        }
    }

    result += parse(eventConfig->vmeScripts["readout_end"]);

    return result;
}

struct DAQReadoutListfileHelperPrivate
{
    QuaZip listfileArchive;
    std::unique_ptr<QIODevice> listfileOut;
    std::unique_ptr<ListFileWriter> listfileWriter;
};

//
// DAQReadoutListfileHelper
//
DAQReadoutListfileHelper::DAQReadoutListfileHelper(VMEReadoutWorkerContext &readoutContext)
    : m_d(std::make_unique<DAQReadoutListfileHelperPrivate>())
    , m_readoutContext(readoutContext)
{
    m_d->listfileWriter = std::make_unique<ListFileWriter>();
}

DAQReadoutListfileHelper::~DAQReadoutListfileHelper()
{
}

QString make_new_listfile_name(ListFileOutputInfo *outInfo)
{
    auto testFlags = (ListFileOutputInfo::UseRunNumber | ListFileOutputInfo::UseTimestamp);
    const bool canModifyName = (outInfo->flags & testFlags);
    QFileInfo fi;
    QString result;

    do
    {
        result = outInfo->fullDirectory + '/' + generate_output_filename(*outInfo);

        fi.setFile(result);

        if (fi.exists())
        {
            if (!canModifyName)
            {
                throw (QString("Listfile output file '%1' exists.")
                       .arg(result));
            }

            if (outInfo->flags & ListFileOutputInfo::UseRunNumber)
            {
                outInfo->runNumber++;
            }
            // otherwise the timestamp will change once one second has passed
        }
    } while (fi.exists());

    return result;
}

void DAQReadoutListfileHelper::beginRun()
{
    if (!m_readoutContext.listfileOutputInfo->enabled)
        return;

    // empty output path
    if (m_readoutContext.listfileOutputInfo->fullDirectory.isEmpty())
        return;

    switch (m_readoutContext.listfileOutputInfo->format)
    {
        case ListFileFormat::Plain:
            {
                QString outFilename = make_new_listfile_name(m_readoutContext.listfileOutputInfo);

                m_d->listfileOut = std::make_unique<QFile>(outFilename);
                auto outFile = reinterpret_cast<QFile *>(m_d->listfileOut.get());

                m_readoutContext.logMessage(QString("Writing to listfile %1").arg(outFilename), false);

                if (!outFile->open(QIODevice::WriteOnly))
                {
                    throw QString("Error opening listFile %1 for writing: %2")
                        .arg(outFile->fileName())
                        .arg(outFile->errorString())
                        ;
                }

                m_d->listfileWriter->setOutputDevice(outFile);
                m_readoutContext.daqStats.listfileFilename = outFilename;
            } break;

        case ListFileFormat::ZIP:
            {
                QString outFilename = make_new_listfile_name(m_readoutContext.listfileOutputInfo);

                /* The name of the listfile inside the zip archive. */
                QFileInfo fi(outFilename);
                QString listfileFilename(QFileInfo(outFilename).completeBaseName());
                listfileFilename += QSL(".mvmelst");

                m_d->listfileArchive.setZipName(outFilename);
                m_d->listfileArchive.setZip64Enabled(true);

                m_readoutContext.logMessage(QString("Writing listfile into %1").arg(outFilename), false);

                if (!m_d->listfileArchive.open(QuaZip::mdCreate))
                {
                    throw make_zip_error(m_d->listfileArchive.getZipName(), m_d->listfileArchive);
                }

                m_d->listfileOut = std::make_unique<QuaZipFile>(&m_d->listfileArchive);
                auto outFile = reinterpret_cast<QuaZipFile *>(m_d->listfileOut.get());

                QuaZipNewInfo zipFileInfo(listfileFilename);
                zipFileInfo.setPermissions(static_cast<QFile::Permissions>(0x6644));

                bool res = outFile->open(QIODevice::WriteOnly, zipFileInfo,
                                         // password, crc
                                         nullptr, 0,
                                         // method (Z_DEFLATED or 0 for no compression)
                                         Z_DEFLATED,
                                         // level
                                         m_readoutContext.listfileOutputInfo->compressionLevel
                                        );

                if (!res)
                {
                    m_d->listfileOut.reset();
                    throw make_zip_error(m_d->listfileArchive.getZipName(), m_d->listfileArchive);
                }

                m_d->listfileWriter->setOutputDevice(m_d->listfileOut.get());
                m_readoutContext.daqStats.listfileFilename = outFilename;

            } break;

            InvalidDefaultCase;
    }

    QJsonObject daqConfigJson;
    m_readoutContext.vmeConfig->write(daqConfigJson);
    QJsonObject configJson;
    configJson["DAQConfig"] = daqConfigJson;
    QJsonDocument doc(configJson);

    if (!m_d->listfileWriter->writePreamble() || !m_d->listfileWriter->writeConfig(doc.toJson()))
    {
        throw_io_device_error(m_d->listfileOut);
    }

    m_readoutContext.daqStats.listFileBytesWritten = m_d->listfileWriter->bytesWritten();
}

void DAQReadoutListfileHelper::endRun()
{
    if (!(m_d->listfileOut && m_d->listfileOut->isOpen()))
        return;


    if (!m_d->listfileWriter->writeEndSection())
    {
        throw_io_device_error(m_d->listfileOut);
    }

    m_readoutContext.daqStats.listFileBytesWritten = m_d->listfileWriter->bytesWritten();

    m_d->listfileOut->close();

    // TODO: more error reporting here (file I/O)
    switch (m_readoutContext.listfileOutputInfo->format)
    {
        case ListFileFormat::Plain:
            {
                // Write a Logfile
                QFile *listFileOut = qobject_cast<QFile *>(m_d->listfileOut.get());
                Q_ASSERT(listFileOut);
                QString logFileName = listFileOut->fileName();
                logFileName.replace(".mvmelst", ".log");
                QFile logFile(logFileName);
                if (logFile.open(QIODevice::WriteOnly))
                {
                    auto messages = m_readoutContext.getLogBuffer();
                    for (const auto &msg: messages)
                    {
                        logFile.write(msg.toLocal8Bit());
                        logFile.write("\n");
                    }
                }
            } break;

        case ListFileFormat::ZIP:
            {

                // Logfile
                {
                    QuaZipNewInfo info("messages.log");
                    info.setPermissions(static_cast<QFile::Permissions>(0x6644));
                    QuaZipFile outFile(&m_d->listfileArchive);

                    bool res = outFile.open(QIODevice::WriteOnly, info,
                                            // password, crc
                                            nullptr, 0,
                                            // method (Z_DEFLATED or 0 for no compression)
                                            0,
                                            // level
                                            m_readoutContext.listfileOutputInfo->compressionLevel
                                           );

                    if (res)
                    {
                        auto messages = m_readoutContext.getLogBuffer();
                        for (const auto &msg: messages)
                        {
                            outFile.write(msg.toLocal8Bit());
                            outFile.write("\n");
                        }
                    }
                }

                // Analysis
                {
                    // TODO: might want to replace this with generate_output_basename() + ".analysis"
                    QuaZipNewInfo info("analysis.analysis");
                    info.setPermissions(static_cast<QFile::Permissions>(0x6644));
                    QuaZipFile outFile(&m_d->listfileArchive);

                    bool res = outFile.open(QIODevice::WriteOnly, info,
                                            // password, crc
                                            nullptr, 0,
                                            // method (Z_DEFLATED or 0 for no compression)
                                            0,
                                            // level
                                            m_readoutContext.listfileOutputInfo->compressionLevel
                                           );

                    if (res)
                    {
                        outFile.write(m_readoutContext.getAnalysisJson().toJson());
                    }
                }

                m_d->listfileArchive.close();

                if (m_d->listfileArchive.getZipError() != UNZ_OK)
                {
                    throw make_zip_error(m_d->listfileArchive.getZipName(), m_d->listfileArchive);
                }
            } break;

            InvalidDefaultCase;
    }

    if (m_readoutContext.listfileOutputInfo->flags & ListFileOutputInfo::UseRunNumber)
    {
        // increment the run number here so that it represents the _next_ run number
        m_readoutContext.listfileOutputInfo->runNumber++;
    }
}

void DAQReadoutListfileHelper::writeBuffer(DataBuffer *buffer)
{
    writeBuffer(buffer->data, buffer->used);
}

void DAQReadoutListfileHelper::writeBuffer(const u8 *buffer, size_t size)
{
    if (m_d->listfileOut && m_d->listfileOut->isOpen())
    {
        if (!m_d->listfileWriter->writeBuffer(reinterpret_cast<const char *>(buffer), size))
        {
            throw_io_device_error(m_d->listfileOut);
        }
        m_readoutContext.daqStats.listFileBytesWritten = m_d->listfileWriter->bytesWritten();
    }
}

void DAQReadoutListfileHelper::writeTimetickSection()
{
    if (m_d->listfileOut && m_d->listfileOut->isOpen())
    {
        if (!m_d->listfileWriter->writeTimetickSection())
        {
            throw_io_device_error(m_d->listfileOut);
        }
        m_readoutContext.daqStats.listFileBytesWritten = m_d->listfileWriter->bytesWritten();
    }
}

void DAQReadoutListfileHelper::writePauseSection()
{
    if (m_d->listfileOut && m_d->listfileOut->isOpen())
    {
        if (!m_d->listfileWriter->writePauseSection(ListfileSections::Pause))
        {
            throw_io_device_error(m_d->listfileOut);
        }
        m_readoutContext.daqStats.listFileBytesWritten = m_d->listfileWriter->bytesWritten();
    }
}

void DAQReadoutListfileHelper::writeResumeSection()
{
    if (m_d->listfileOut && m_d->listfileOut->isOpen())
    {
        if (!m_d->listfileWriter->writePauseSection(ListfileSections::Resume))
        {
            throw_io_device_error(m_d->listfileOut);
        }
        m_readoutContext.daqStats.listFileBytesWritten = m_d->listfileWriter->bytesWritten();
    }
}

bool has_errors(const QVector<ScriptWithResult> &results)
{
    for (const auto &swr: results)
    {
        for (auto &result: swr.results)
        {
            if (result.error.isError())
                return true;
        }
    }

    return false;
}

void log_errors(const QVector<ScriptWithResult> &results,
                std::function<void (const QString &)> logger)
{
    for (const auto &swr: results)
    {
        const auto &script = swr.scriptConfig;

        for (auto &result: swr.results)
        {
            if (result.error.isError())
            {
                QString msg = QSL("Error from '%1': %2")
                    .arg(to_string(result.command))
                    .arg(result.error.toString())
                    ;
                logger(msg);
            }
        }
    }
}
