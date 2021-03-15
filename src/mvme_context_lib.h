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
#ifndef __MVME_CONTEXT_LIB_H__
#define __MVME_CONTEXT_LIB_H__

#include "mvme_listfile_utils.h"
#include "mvme_context.h"

// Saving of vme and analysis configs.
//
// The returned boolean is true if the config was saved or the user wants to
// discard pending changes. False if the config could not be saved or the user
// wants to cancel the current action.
// The second member of the return value contains the name of the file that was
// written.

QPair<bool, QString> gui_saveAnalysisConfig(analysis::Analysis *analysis_ng,
                                        const QString &fileName,
                                        QString startPath,
                                        QString fileFilter);

QPair<bool, QString> gui_saveAnalysisConfigAs(analysis::Analysis *analysis_ng,
                                          QString startPath,
                                          QString fileFilter);

// These add vme properties from the vme config to the analysis then call the
// gui_saveAnalysisConfig*() functions.
QPair<bool, QString> saveAnalysisConfig(analysis::Analysis *analysis,
                                        const QString &fileName,
                                        QString startPath,
                                        QString fileFilter,
                                        MVMEContext *context);

QPair<bool, QString> saveAnalysisConfigAs(analysis::Analysis *analysis,
                                           QString startPath,
                                           QString fileFilter,
                                           MVMEContext *context);

// These get the VMEConfig/Analysis object from the context. If the object is
// modified the save -> saveas sequence is run.
QPair<bool, QString> analysis_maybe_save_if_modified(MVMEContext *context);



QPair<bool, QString> save_vme_config(VMEConfig *vmeConfig, const QString &filename, QString startPath);
QPair<bool, QString> save_vme_config_as(VMEConfig *vmeConfig, QString startPath);

QPair<bool, QString> vmeconfig_maybe_save_if_modified(MVMEContext *context);



// listfile opening
struct OpenListfileFlags
{
    static const u16 LoadAnalysis = 1u << 0;
};

/* IMPORTANT: Does not check if the current analysis is modified before loading
 * one from the listfile. Perform this check before calling this function!. */
LIBMVME_EXPORT const ListfileReplayHandle &context_open_listfile(
    MVMEContext *context, const QString &filename, u16 flags = 0);

struct AnalysisPauser
{
    explicit AnalysisPauser(MVMEContext *context);
    ~AnalysisPauser();

    MVMEContext *m_context;
    AnalysisWorkerState m_prevState;
};

void LIBMVME_EXPORT new_vme_config(MVMEContext *context);

#endif /* __MVME_CONTEXT_LIB_H__ */
