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
            if (script->isEnabled())
            {
                logger(QString("  %1").arg(script->objectName()));
                auto indentingLogger = [logger](const QString &str) { logger(QSL("    ") + str); };
                run_script(controller, script->getScript(), indentingLogger, true);
            }
        }
    }

    logger(QSL(""));
    logger(QSL("Initializing Modules:"));
    for (auto eventConfig: config->getEventConfigs())
    {
        // XXX: VMEEnable
        //if (!eventConfig->isEnabled())
        //    continue;

        for (auto module: eventConfig->getModuleConfigs())
        {
            // XXX: VMEEnable
            //if (!module->isEnabled())
            //    continue;

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
        // XXX: VMEEnable
        //if (!eventConfig->isEnabled())
        //    continue;

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
        // XXX: VMEEnable
        //if (!eventConfig->isEnabled())
        //    continue;

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
            // XXX: VMEEnable
            //if (!script->isEnabled())
            //    continue;

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
    // XXX: VMEEnable
    //Q_ASSERT(eventConfig->isEnabled());

    using namespace vme_script;

    VMEScript result;

    result += eventConfig->vmeScripts["readout_start"]->getScript();

    for (auto module: eventConfig->getModuleConfigs())
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

    static void throw_io_device_error(std::unique_ptr<QIODevice> &device)
    {
        throw_io_device_error(device.get());
    }
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
DAQReadoutListfileHelper::DAQReadoutListfileHelper(
    VMEReadoutWorkerContext readoutContext, QObject *parent)
    : QObject(parent)
    , m_d(new DAQReadoutListfileHelperPrivate)
    , m_readoutContext(readoutContext)
{
    m_d->listfileWriter = std::make_unique<ListFileWriter>();
}

DAQReadoutListfileHelper::~DAQReadoutListfileHelper()
{
    delete m_d;
}

namespace
{
/* Throws if neither UseRunNumber nor UseTimestamp is set and the file already
 * exists. Otherwise tries until it hits a non-existant filename. In the odd
 * case where a timestamped filename exists and only UseTimestamp is set this
 * process will take 1s!
 *
 * Also note that the file handling code does not in any way guard against race
 * conditions when someone else is also creating files.
 *
 * Note: Increments the runNumer of outInfo if UseRunNumber is set in the
 * output flags.
 */
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

} // end anon namespace

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

                m_readoutContext.logMessage(QString("Writing to listfile %1").arg(outFilename));

                if (!outFile->open(QIODevice::WriteOnly))
                {
                    throw QString("Error opening listFile %1 for writing: %2")
                        .arg(outFile->fileName())
                        .arg(outFile->errorString())
                        ;
                }

                m_d->listfileWriter->setOutputDevice(outFile);
                m_readoutContext.daqStats->listfileFilename = outFilename;
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

                m_readoutContext.logMessage(QString("Writing listfile into %1").arg(outFilename));

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
                m_readoutContext.daqStats->listfileFilename = outFilename;

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

    m_readoutContext.daqStats->listFileBytesWritten = m_d->listfileWriter->bytesWritten();
}

void DAQReadoutListfileHelper::endRun()
{
    if (!(m_d->listfileOut && m_d->listfileOut->isOpen()))
        return;


    if (!m_d->listfileWriter->writeEndSection())
    {
        throw_io_device_error(m_d->listfileOut);
    }

    m_readoutContext.daqStats->listFileBytesWritten = m_d->listfileWriter->bytesWritten();

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
        m_readoutContext.daqStats->listFileBytesWritten = m_d->listfileWriter->bytesWritten();
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
        m_readoutContext.daqStats->listFileBytesWritten = m_d->listfileWriter->bytesWritten();
    }
}
