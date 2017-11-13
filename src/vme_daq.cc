/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016, 2017  Florian Lüke <f.lueke@mesytec.com>
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
#include <quazipfile.h>
#include <quazip.h>

#include "mvme_listfile.h"

//
// vme_daq_init
//
void vme_daq_init(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger)
{
    auto startScripts = config->vmeScriptLists["daq_start"];
    if (!startScripts.isEmpty())
    {
        logger(QSL(""));
        logger(QSL("Global DAQ Start scripts:"));
        for (auto script: startScripts)
        {
            logger(QString("  %1").arg(script->objectName()));
            auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
            run_script(controller, script->getScript(), indentingLogger, true);
        }
    }

    logger(QSL(""));
    logger(QSL("Initializing Modules:"));
    for (auto eventConfig: config->getEventConfigs())
    {
        for (auto module: eventConfig->modules)
        {
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
                run_script(controller, scriptConfig->getScript(module->getBaseAddress()), indentingLogger, true);
            }
        }
    }

    logger(QSL("Events DAQ Start"));
    for (auto eventConfig: config->getEventConfigs())
    {
        logger(QString("  %1").arg(eventConfig->objectName()));
        auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
        run_script(controller, eventConfig->vmeScripts["daq_start"]->getScript(), indentingLogger, true);
    }
}

//
// vme_daq_shutdown
//
void vme_daq_shutdown(
    VMEConfig *config,
    VMEController *controller,
    std::function<void (const QString &)> logger)
{
    logger(QSL("Events DAQ Stop"));
    for (auto eventConfig: config->getEventConfigs())
    {
        logger(QString("  %1").arg(eventConfig->objectName()));
        auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
        run_script(controller, eventConfig->vmeScripts["daq_stop"]->getScript(), indentingLogger, true);
    }

    auto stopScripts = config->vmeScriptLists["daq_stop"];
    if (!stopScripts.isEmpty())
    {
        logger(QSL("Global DAQ Stop scripts:"));
        for (auto script: stopScripts)
        {
            logger(QString("  %1").arg(script->objectName()));
            auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
            run_script(controller, script->getScript(), indentingLogger, true);
        }
    }
}

//
// build_event_readout_script
//
vme_script::VMEScript build_event_readout_script(EventConfig *eventConfig)
{
    using namespace vme_script;

    VMEScript result;

    result += eventConfig->vmeScripts["readout_start"]->getScript();

    for (auto module: eventConfig->modules)
    {
        result += module->getReadoutScript()->getScript(module->getBaseAddress());
        Command marker;
        marker.type = CommandType::Marker;
        marker.value = EndMarker;
        result += marker;
    }

    result += eventConfig->vmeScripts["readout_end"]->getScript();

    return result;
}

namespace
{
    static std::runtime_error make_zip_error(const QString &msg, const QuaZip &zip)
    {
        auto m = QString("Error: archive=%1, error=%2")
            .arg(msg)
            .arg(zip.getZipError());

        return std::runtime_error(m.toStdString());
    }

    static void throw_io_device_error(QIODevice *device)
    {
        if (auto zipFile = qobject_cast<QuaZipFile *>(device))
        {
            throw make_zip_error(zipFile->getZip()->getZipName(),
                                 *(zipFile->getZip()));
        }
        else if (auto file = qobject_cast<QFile *>(device))
        {
            throw QString("Error: file=%1, error=%2")
                .arg(file->fileName())
                .arg(file->errorString())
                ;
        }
        else
        {
            throw QString("IO Error: %1")
                .arg(device->errorString());
        }
    }
}

struct DAQReadoutListfileHelperPrivate
{
    QuaZip m_listFileArchive;
    QIODevice *m_listFileOut = nullptr;
    ListFileWriter *m_listfileWriter = nullptr;
};

//
// DAQReadoutListfileHelper
//
DAQReadoutListfileHelper::DAQReadoutListfileHelper(VMEReadoutWorkerContext readoutContext,
                                                   QObject *parent)
: QObject(parent)
, m_d(new DAQReadoutListfileHelperPrivate)
, m_readoutContext(readoutContext)
{
    m_d->m_listfileWriter = new ListFileWriter(this);
}

DAQReadoutListfileHelper::~DAQReadoutListfileHelper()
{
    delete m_d;
}

void DAQReadoutListfileHelper::beginRun()
{
    QString outPath = m_readoutContext.listfileOutputInfo->fullDirectory;
    bool listFileOutputEnabled = m_readoutContext.listfileOutputInfo->enabled;

    if (listFileOutputEnabled && !outPath.isEmpty())
    {
        delete m_d->m_listFileOut;
        m_d->m_listFileOut = nullptr;

        const QString outputFilename = generate_output_filename(*m_readoutContext.listfileOutputInfo);
        const QString outFilename = outPath + '/' + outputFilename;

        switch (m_readoutContext.listfileOutputInfo->format)
        {
            case ListFileFormat::Plain:
                {
                    QFile *outFile = new QFile(this);
                    outFile->setFileName(outFilename);
                    m_d->m_listFileOut = outFile;

                    m_readoutContext.logMessage(QString("Writing to listfile %1").arg(outFilename));

                    // TODO: increment run number if it is used in the filename
                    if (outFile->exists())
                    {
                        throw QString("Error: listFile %1 exists").arg(outFilename);
                    }

                    if (!outFile->open(QIODevice::WriteOnly))
                    {
                        throw QString("Error opening listFile %1 for writing: %2")
                            .arg(outFile->fileName())
                            .arg(outFile->errorString())
                            ;
                    }

                    m_d->m_listfileWriter->setOutputDevice(outFile);
                    m_readoutContext.daqStats->listfileFilename = outFilename;
                } break;

            case ListFileFormat::ZIP:
                {
                    QFileInfo fi(outFilename);
                    if (fi.exists())
                    {
                        throw QString("Error: listFile %1 exists").arg(outFilename);
                    }

                    m_d->m_listFileArchive.setZipName(outFilename);
                    m_d->m_listFileArchive.setZip64Enabled(true);

                    m_readoutContext.logMessage(QString("Writing listfile into %1").arg(outFilename));

                    if (!m_d->m_listFileArchive.open(QuaZip::mdCreate))
                    {
                        throw make_zip_error(m_d->m_listFileArchive.getZipName(), m_d->m_listFileArchive);
                    }

                    auto outFile = new QuaZipFile(&m_d->m_listFileArchive, this);
                    m_d->m_listFileOut = outFile;

                    QuaZipNewInfo zipFileInfo(m_readoutContext.runInfo->runId + QSL(".mvmelst"));
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
                        delete m_d->m_listFileOut;
                        m_d->m_listFileOut = nullptr;
                        throw make_zip_error(m_d->m_listFileArchive.getZipName(), m_d->m_listFileArchive);
                    }

                    m_d->m_listfileWriter->setOutputDevice(m_d->m_listFileOut);
                    m_readoutContext.daqStats->listfileFilename = outFilename;

                } break;

                InvalidDefaultCase;
        }

        QJsonObject daqConfigJson;
        m_readoutContext.vmeConfig->write(daqConfigJson);
        QJsonObject configJson;
        configJson["DAQConfig"] = daqConfigJson;
        QJsonDocument doc(configJson);

        if (!m_d->m_listfileWriter->writePreamble() || !m_d->m_listfileWriter->writeConfig(doc.toJson()))
        {
            throw_io_device_error(m_d->m_listFileOut);
        }

        m_readoutContext.daqStats->listFileBytesWritten = m_d->m_listfileWriter->bytesWritten();
    }
}

void DAQReadoutListfileHelper::endRun()
{
    if (m_d->m_listFileOut && m_d->m_listFileOut->isOpen())
    {
        if (!m_d->m_listfileWriter->writeEndSection())
        {
            throw_io_device_error(m_d->m_listFileOut);
        }

        m_readoutContext.daqStats->listFileBytesWritten = m_d->m_listfileWriter->bytesWritten();

        m_d->m_listFileOut->close();

        // TODO: more error reporting here (file I/O)
        switch (m_readoutContext.listfileOutputInfo->format)
        {
            case ListFileFormat::Plain:
                {
                    // Write a Logfile
                    QFile *listFileOut = qobject_cast<QFile *>(m_d->m_listFileOut);
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
                        QuaZipFile outFile(&m_d->m_listFileArchive);

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
                        QuaZipFile outFile(&m_d->m_listFileArchive);

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

                    m_d->m_listFileArchive.close();

                    if (m_d->m_listFileArchive.getZipError() != UNZ_OK)
                    {
                        throw make_zip_error(m_d->m_listFileArchive.getZipName(), m_d->m_listFileArchive);
                    }
                } break;

                InvalidDefaultCase;
        }

        // increment the run number here so that it represents the _next_ run number
        if (m_readoutContext.listfileOutputInfo->flags & ListFileOutputInfo::UseRunNumber)
        {
            m_readoutContext.listfileOutputInfo->runNumber++;
        }
    }
}

void DAQReadoutListfileHelper::writeBuffer(DataBuffer *buffer)
{
    writeBuffer(buffer->data, buffer->used);
}

void DAQReadoutListfileHelper::writeBuffer(const u8 *buffer, size_t size)
{
    if (m_d->m_listFileOut && m_d->m_listFileOut->isOpen())
    {
        if (!m_d->m_listfileWriter->writeBuffer(reinterpret_cast<const char *>(buffer), size))
        {
            throw_io_device_error(m_d->m_listFileOut);
        }
        m_readoutContext.daqStats->listFileBytesWritten = m_d->m_listfileWriter->bytesWritten();
    }
}
