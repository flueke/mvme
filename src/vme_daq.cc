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

#if __WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include "mvme_listfile_utils.h"
#include "util/assert.h"
#include "util_zip.h"
#include "vme_config_scripts.h"
#include "vme_config_util.h"
#include "vme_script.h"

using namespace mesytec::mvme;
using namespace vme_script::run_script_options;

#if __WIN32
static const unsigned Win32TimePeriod = 1;
#endif

QVector<ScriptWithResults>
vme_daq_run_global_daq_start_scripts(
    const VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts)
{
    QVector<ScriptWithResults> ret;

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
            auto indentingErrorLogger = [errorLogger](const QString &str) { errorLogger(QSL("    ") + str); };

            try
            {
                auto script = parse(scriptConfig);
                auto results = run_script(controller, script, indentingLogger, indentingErrorLogger,
                    opts | LogEachResult);

                ret.push_back({ scriptConfig, results});

                if ((opts & AbortOnError) && has_errors(results))
                    return ret;
            }
            catch (const vme_script::ParseError &e)
            {
                ret.push_back({ scriptConfig, {}, std::make_shared<vme_script::ParseError>(e)});

                if (opts & AbortOnError)
                    return ret;
            }
        }
    }

    return ret;
}

QVector<ScriptWithResults>
vme_daq_run_global_daq_stop_scripts(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts)
{
    QVector<ScriptWithResults> ret;

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
            auto indentingErrorLogger = [errorLogger](const QString &str) { errorLogger(QSL("    ") + str); };
            try
            {
                auto script = parse(scriptConfig);
                auto results = run_script(
                    controller, script,
                    indentingLogger, indentingErrorLogger,
                    opts | LogEachResult);

                ret.push_back({ scriptConfig, results });
                if ((opts & AbortOnError) && has_errors(results))
                    return ret;
            }
            catch (const vme_script::ParseError &e)
            {
                ret.push_back({ scriptConfig, {}, std::make_shared<vme_script::ParseError>(e)});

                if (opts & AbortOnError)
                    return ret;
            }
        }
    }

    return ret;
}

QVector<ScriptWithResults>
vme_daq_run_init_modules(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts)
{
    QVector<ScriptWithResults> ret;

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
                auto indentingErrorLogger = [errorLogger](const QString &str) { errorLogger(QSL("      ") + str); };

                try
                {
                    auto script = parse(scriptConfig, module->getBaseAddress());
                    auto results = run_script(
                        controller, script,
                        indentingLogger, indentingErrorLogger, opts | LogEachResult);

                    ret.push_back({ scriptConfig, results });

                    if ((opts & AbortOnError) && has_errors(results))
                        return ret;
                }
                catch (const vme_script::ParseError &e)
                {
                    ret.push_back({ scriptConfig, {}, std::make_shared<vme_script::ParseError>(e)});

                    if (opts & AbortOnError)
                        return ret;
                }
            }
        }
    }

    return ret;
}

QVector<ScriptWithResults>
vme_daq_run_event_daq_start_scripts(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts)
{
    QVector<ScriptWithResults> ret;

    logger(QSL("Running Event DAQ Start Scripts"));
    for (auto eventConfig: config->getEventConfigs())
    {
        auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
        auto indentingErrorLogger = [errorLogger](const QString &str) { errorLogger(QSL("    ") + str); };
        auto scriptConfig = eventConfig->vmeScripts["daq_start"];

        try
        {
            auto script = parse(scriptConfig);

            if (!script.isEmpty())
                logger(QString("  %1").arg(eventConfig->objectName()));

            auto results = run_script(controller, script, indentingLogger, indentingErrorLogger, opts | LogEachResult);
            ret.push_back({ scriptConfig, results });
            if ((opts & AbortOnError) && has_errors(results))
                return ret;
        }
        catch (const vme_script::ParseError &e)
        {
            ret.push_back({ scriptConfig, {}, std::make_shared<vme_script::ParseError>(e)});

            if (opts & AbortOnError)
                return ret;
        }
    }

    return ret;
}

QVector<ScriptWithResults>
vme_daq_run_event_daq_stop_scripts(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts)
{
    QVector<ScriptWithResults> ret;

    logger(QSL("Running Event DAQ Stop Scripts"));
    for (auto eventConfig: config->getEventConfigs())
    {
        logger(QString("  %1").arg(eventConfig->objectName()));
        auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
        auto indentingErrorLogger = [errorLogger](const QString &str) { errorLogger(QSL("    ") + str); };
        auto scriptConfig = eventConfig->vmeScripts["daq_stop"];

        try
        {
            auto script = parse(scriptConfig);
            auto results = run_script(
                controller, script,
                indentingLogger, indentingErrorLogger,
                opts | LogEachResult);

            ret.push_back({ scriptConfig, results });
            if ((opts & AbortOnError) && has_errors(results))
                return ret;
        }
        catch (const vme_script::ParseError &e)
        {
            ret.push_back({ scriptConfig, {}, std::make_shared<vme_script::ParseError>(e)});

            if (opts & AbortOnError)
                return ret;
        }
    }

    return ret;
}

//
// vme_daq_init
//
QVector<ScriptWithResults>
vme_daq_init(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    vme_script::run_script_options::Flag opts
    )
{
    return vme_daq_init(
        config,
        controller,
        logger,
        logger,
        opts);
}

QVector<ScriptWithResults>
vme_daq_init(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts
    )
{
    using namespace vme_script::run_script_options;

    QVector<ScriptWithResults> ret;

    ret += vme_daq_run_global_daq_start_scripts(config, controller, logger, errorLogger, opts);
    ret += vme_daq_run_init_modules(config, controller, logger, errorLogger, opts);
    ret += vme_daq_run_event_daq_start_scripts(config, controller, logger, errorLogger, opts);

#if __WIN32
    TRY_ASSERT(timeBeginPeriod(Win32TimePeriod) == TIMERR_NOERROR);
#endif

    return ret;
}

//
// vme_daq_shutdown
//
QVector<ScriptWithResults>
vme_daq_shutdown(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    vme_script::run_script_options::Flag opts
    )
{
    return vme_daq_shutdown(config, controller, logger, logger, opts);
}

QVector<ScriptWithResults>
vme_daq_shutdown(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts
    )
{
#if __WIN32
    TRY_ASSERT(timeEndPeriod(Win32TimePeriod) == TIMERR_NOERROR);
#endif

    logger(QSL("DAQ stopped on %1")
           .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    logger("");

    QVector<ScriptWithResults> ret;

    ret += vme_daq_run_event_daq_stop_scripts(config, controller, logger, errorLogger, opts);
    ret += vme_daq_run_global_daq_stop_scripts(config, controller, logger, errorLogger, opts);

    return ret;
}

//
// mvlc_daq_shutdown
//
QVector<ScriptWithResults>
mvlc_daq_shutdown(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger,
    std::function<void (const QString &)> errorLogger,
    vme_script::run_script_options::Flag opts
    )
{
    logger(QSL("DAQ stopped on %1")
           .arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    logger("");

    QVector<ScriptWithResults> ret;
    ret += vme_daq_run_global_daq_stop_scripts(config, controller, logger, errorLogger, opts);
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
    const bool canModifyName = (outInfo->flags & (ListFileOutputInfo::UseRunNumber | ListFileOutputInfo::UseTimestamp));
    const bool usesSplitting = (outInfo->flags & (ListFileOutputInfo::SplitBySize | ListFileOutputInfo::SplitByTime));
    QFileInfo fi;

    do
    {
        QString nameToTest;

        if (usesSplitting)
        {
            auto filename = generate_output_filename(*outInfo);
            auto basename = QFileInfo(filename).completeBaseName();
            auto suffix = QFileInfo(filename).completeSuffix();
            nameToTest = outInfo->fullDirectory + '/' + basename + "_part001." + suffix;
        }
        else
            nameToTest = outInfo->fullDirectory + '/' + generate_output_filename(*outInfo);

        fi.setFile(nameToTest);

        if (fi.exists())
        {
            if (!canModifyName)
                throw QString("Listfile output file '%1' exists.").arg(nameToTest);

            if (outInfo->flags & ListFileOutputInfo::UseRunNumber)
                outInfo->runNumber++;
            // In the case of UseTimestamp the timestamp value will change once
            // one second has passed.
        }
    } while (fi.exists());

    auto result = outInfo->fullDirectory + '/' + generate_output_filename(*outInfo);
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

    auto doc = mvme::vme_config::serialize_vme_config_to_json_document(*m_readoutContext.vmeConfig);

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

                // run_notes
                {
                    QuaZipNewInfo info("mvme_run_notes.txt");
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
                        outFile.write(m_readoutContext.getRunNotes().toLocal8Bit());
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

bool has_errors(const QVector<ScriptWithResults> &results)
{
    for (const auto &swr: results)
    {
        if (has_errors(swr.results) || swr.parseError)
            return true;
    }

    return false;
}

void log_errors(const QVector<ScriptWithResults> &results,
                std::function<void (const QString &)> logger)
{
    for (const auto &swr: results)
    {
        if (swr.parseError)
        {
            logger(QSL("Error parsing '%1': %2")
                   .arg(swr.scriptConfig->getVerboseTitle())
                   .arg(swr.parseError->toString()));
        }
        else
        {
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
}

QVector<VMEScriptConfig *> collect_event_mcst_daq_start_scripts(const VMEConfig *vmeConfig)
{
    QVector<VMEScriptConfig *> result;

    for (const auto &eventConfig: vmeConfig->getEventConfigs())
    {
        if (auto script = eventConfig->vmeScripts["daq_start"])
            result.push_back(script);
    }

    return result;
}

QVector<VMEScriptConfig *> collect_event_mcst_daq_stop_scripts(const VMEConfig *vmeConfig)
{
    QVector<VMEScriptConfig *> result;

    for (const auto &eventConfig: vmeConfig->getEventConfigs())
    {
        if (auto script = eventConfig->vmeScripts["daq_stop"])
            result.push_back(script);
    }

    return result;
}
