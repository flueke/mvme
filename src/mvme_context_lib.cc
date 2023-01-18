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
#include "mvme_context_lib.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>

#include "analysis/analysis.h"
#include "mvme_context.h"
#include "template_system.h"
#include "util_zip.h"
#include "vme_config_util.h"


namespace
{

bool write_json_to_file(const QString &filename, const QJsonDocument &doc)
{
    QFile outfile(filename);

    if (!outfile.open(QIODevice::WriteOnly))
        return false;

    auto data = doc.toJson();
    auto written = outfile.write(data);

    return written == data.size();
}

static const QString AnalysisFileFilter = QSL(
    "MVME Analysis Files (*.analysis);; All Files (*.*)");

static const QString VMEConfigFileFilter = QSL(
    "MVME VME Config Files (*.vme *.mvmecfg);; All Files (*.*)");

bool gui_save_analysis_impl(analysis::Analysis *analysis_ng, const QString &fileName)
{
    auto doc = analysis::serialize_analysis_to_json_document(*analysis_ng);
    return gui_write_json_file(fileName, doc);
}

bool gui_save_vmeconfig_impl(VMEConfig *vmeConfig, const QString &filename)
{
    auto doc = mvme::vme_config::serialize_vme_config_to_json_document(*vmeConfig);
    return gui_write_json_file(filename, doc);
}

} // end anon namespace

//
// non-gui write functions
//
bool write_vme_config_to_file(const QString &filename, const VMEConfig *vmeConfig)
{
    auto doc = mvme::vme_config::serialize_vme_config_to_json_document(*vmeConfig);
    return write_json_to_file(filename, doc);
}

bool write_analysis_to_file(const QString &filename, const analysis::Analysis *analysis)
{
    auto doc = analysis::serialize_analysis_to_json_document(*analysis);
    return write_json_to_file(filename, doc);
}


//
// analysis
//

QPair<bool, QString> gui_save_analysis_config(
    analysis::Analysis *analysis,
    const QString &fileName,
    QString startPath,
    QString fileFilter,
    AnalysisServiceProvider *serviceProvider)
{
    vme_analysis_common::update_analysis_vme_properties(serviceProvider->getVMEConfig(), analysis);

    if (fileName.isEmpty())
        return gui_save_analysis_config_as(analysis, startPath, fileFilter, serviceProvider);

    if (gui_save_analysis_impl(analysis, fileName))
        return qMakePair(true, fileName);

    return qMakePair(false, QString());
}

QPair<bool, QString> gui_save_analysis_config_as(
    analysis::Analysis *analysis,
    QString path,
    QString fileFilter,
    AnalysisServiceProvider *serviceProvider)
{
    vme_analysis_common::update_analysis_vme_properties(serviceProvider->getVMEConfig(), analysis);

    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QFileDialog fd(nullptr, QSL("Save analysis config"), path, fileFilter);
    fd.setDefaultSuffix(".analysis");
    fd.setAcceptMode(QFileDialog::AcceptMode::AcceptSave);

    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty())
        return qMakePair(false, QString());

    auto fileName = fd.selectedFiles().front();

    if (gui_save_analysis_impl(analysis, fileName))
        return qMakePair(true, fileName);

    return qMakePair(false, QString());
}

QPair<bool, QString> gui_analysis_maybe_save_if_modified(AnalysisServiceProvider *serviceProvider)
{
    auto result = qMakePair(true, QString());

    auto analysis = serviceProvider->getAnalysis();

    if (analysis->isModified())
    {
        QMessageBox msgBox(
            QMessageBox::Question,
            QSL("Save analysis configuration?"),
            QSL("The current analysis configuration has modifications. Do you want to save it?"),
            QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int choice = msgBox.exec();

        if (choice == QMessageBox::Save)
        {
            result = gui_save_analysis_config(
                analysis,
                serviceProvider->getAnalysisConfigFilename(),
                serviceProvider->getWorkspaceDirectory(),
                AnalysisFileFilter,
                serviceProvider);

            if (result.first)
            {
                analysis->setModified(false);
                serviceProvider->setAnalysisConfigFilename(result.second);
                serviceProvider->analysisWasSaved();
            }
        }
        else if (choice == QMessageBox::Cancel)
        {
            result.first = false;
        }
        else
        {
            assert(choice == QMessageBox::Discard);
        }
    }

    return result;
}

//
// vme config
//

QPair<bool, QString> gui_save_vme_config(
    VMEConfig *vmeConfig,
    const QString &filename,
    QString startPath)
{
    if (filename.isEmpty())
        return gui_save_vme_config_as(vmeConfig, startPath);

    if (gui_save_vmeconfig_impl(vmeConfig, filename))
        return qMakePair(true, filename);

    return qMakePair(false, QString());
}

QPair<bool, QString> gui_save_vme_config_as(
    VMEConfig *vmeConfig,
    QString path)
{
    if (path.isEmpty())
        path = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation).at(0);

    QString filename = QFileDialog::getSaveFileName(
        nullptr, "Save VME Config As", path, VMEConfigFileFilter);

    if (filename.isEmpty())
        return qMakePair(false, QString());

    if (gui_save_vmeconfig_impl(vmeConfig, filename))
        return qMakePair(true, filename);

    return qMakePair(false, QString());
}

QPair<bool, QString> gui_vmeconfig_maybe_save_if_modified(AnalysisServiceProvider *serviceProvider)
{
    auto result = qMakePair(true, QString());

    auto vmeConfig = serviceProvider->getVMEConfig();

    if (vmeConfig->isModified())
    {
        QMessageBox msgBox(
            QMessageBox::Question,
            QSL("Save VME configuration?"),
            QSL("The current VME configuration has modifications. Do you want to save it?"),
            QMessageBox::Save | QMessageBox::Cancel | QMessageBox::Discard);

        int choice = msgBox.exec();

        if (choice == QMessageBox::Save)
        {
            result = gui_save_vme_config(
                vmeConfig,
                serviceProvider->getVMEConfigFilename(),
                serviceProvider->getWorkspaceDirectory());

            if (result.first)
            {
                vmeConfig->setModified(false);
                serviceProvider->setVMEConfigFilename(result.second);
                serviceProvider->vmeConfigWasSaved();
            }
        }
        else if (choice == QMessageBox::Cancel)
        {
            result.first = false;
        }
        else
        {
            assert(choice == QMessageBox::Discard);
        }
    }

    return result;
}

const ListfileReplayHandle &context_open_listfile(
    MVMEContext *context,
    const QString &filename,
    OpenListfileOptions options)
{
    // save current replay state and set new listfile on the context object
    bool wasReplaying = (context->getMode() == GlobalMode::ListFile
                         && context->getDAQState() == DAQState::Running);

    auto handle = open_listfile(filename);

    // Transfers ownership to the context.
    context->setReplayFileHandle(std::move(handle), options);

    if (wasReplaying)
    {
        context->startDAQReplay();
    }

    return context->getReplayFileHandle();
}

// Returns true if the given listfile zip archive contains a file called
// "analysis.analysis".
bool listfile_contains_analysis(const QString &listfileArchivePath)
{
    auto handle = open_listfile(listfileArchivePath);
    return !handle.analysisBlob.isEmpty();
}

bool listfile_is_archive_corrupted(const QString &listfileArchivePath)
{
    QFileInfo fi(listfileArchivePath);
    const bool isFile = fi.isFile();
    const bool isReadable = fi.isReadable();

    if (isFile && isReadable)
    {
        // assume the zip is corrupted if quazip cannot open it
        return !QuaZip(listfileArchivePath).open(QuaZip::mdUnzip);
    }

    // Cannot determine anything.
    return false;
}

//
// AnalysisPauser
//
AnalysisPauser::AnalysisPauser(AnalysisServiceProvider *serviceProvider)
    : m_serviceProvider(serviceProvider)
    , m_prevState(m_serviceProvider->getAnalysisWorkerState())
{
    qDebug() << __PRETTY_FUNCTION__ << "prevState =" << to_string(m_prevState);

    switch (m_prevState)
    {
        case AnalysisWorkerState::Running:
            m_serviceProvider->stopAnalysis();
            break;

        case AnalysisWorkerState::Idle:
        case AnalysisWorkerState::Paused:
        case AnalysisWorkerState::SingleStepping:
            break;
    }
}

AnalysisPauser::~AnalysisPauser()
{
    qDebug() << __PRETTY_FUNCTION__ << "prevState =" << to_string(m_prevState);

    switch (m_prevState)
    {
        case AnalysisWorkerState::Running:
            m_serviceProvider->resumeAnalysis(analysis::Analysis::KeepState);
            break;

        case AnalysisWorkerState::Idle:
        case AnalysisWorkerState::Paused:
        case AnalysisWorkerState::SingleStepping:
            {
                auto analysis = m_serviceProvider->getAnalysis();

                if (analysis->anyObjectNeedsRebuild())
                {
                    qDebug() << __PRETTY_FUNCTION__
                        << "rebuilding analysis because at least one object needs a rebuild";
                    analysis->beginRun(
                        analysis::Analysis::KeepState, m_serviceProvider->getVMEConfig(),
                        [this] (const QString &msg) { m_serviceProvider->logMessage(msg); });
                }
            }
            break;
    }
}

void LIBMVME_EXPORT new_vme_config(MVMEContext *context)
{
    // copy the previous controller settings into the new VMEConfig
    auto vmeConfig = context->getVMEConfig();
    auto ctrlType = vmeConfig->getControllerType();
    auto ctrlSettings = vmeConfig->getControllerSettings();

    vmeConfig = new VMEConfig;
    vmeConfig->setVMEController(ctrlType, ctrlSettings);

    // If the new controller is an MVLC load the default trigger io scripts
    // from the templates directory.
    if (is_mvlc_controller(ctrlType))
    {
        if (auto mvlcTriggerIO = vmeConfig->getGlobalObjectRoot().findChild<VMEScriptConfig *>(
                QSL("mvlc_trigger_io")))
        {
            mvlcTriggerIO->setScriptContents(vats::read_default_mvlc_trigger_io_script().contents);
        }
    }

    context->setVMEConfig(vmeConfig);
    context->setVMEConfigFilename(QString());
    context->setMode(GlobalMode::DAQ);
}
