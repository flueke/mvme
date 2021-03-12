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
#include "mvme_context_lib.h"
#include "analysis/analysis.h"
#include "mvme_context.h"
#include "template_system.h"
#include "util_zip.h"

const ListfileReplayHandle &context_open_listfile(
    MVMEContext *context,
    const QString &filename, u16 flags)
{
    // save current replay state and set new listfile on the context object
    bool wasReplaying = (context->getMode() == GlobalMode::ListFile
                         && context->getDAQState() == DAQState::Running);

    auto handle = open_listfile(filename);

    // Transfers ownership to the context.
    context->setReplayFileHandle(std::move(handle), flags);

    if (wasReplaying)
    {
        context->startDAQReplay();
    }

    return context->getReplayFileHandle();
}

//
// AnalysisPauser
//
AnalysisPauser::AnalysisPauser(MVMEContext *context)
    : m_context(context)
    , m_prevState(m_context->getMVMEStreamWorkerState())
{
    qDebug() << __PRETTY_FUNCTION__ << "prevState =" << to_string(m_prevState);

    switch (m_prevState)
    {
        case AnalysisWorkerState::Running:
            m_context->stopAnalysis();
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
            m_context->resumeAnalysis(analysis::Analysis::KeepState);
            break;

        case AnalysisWorkerState::Idle:
        case AnalysisWorkerState::Paused:
        case AnalysisWorkerState::SingleStepping:
            {
                auto analysis = m_context->getAnalysis();

                if (analysis->anyObjectNeedsRebuild())
                {
                    qDebug() << __PRETTY_FUNCTION__
                        << "rebuilding analysis because at least one object needs a rebuild";
                    analysis->beginRun(
                        analysis::Analysis::KeepState, m_context->getVMEConfig(),
                        [this] (const QString &msg) { m_context->logMessage(msg); });

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
    context->setConfigFileName(QString());
    context->setMode(GlobalMode::DAQ);
}
