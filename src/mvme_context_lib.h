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
#ifndef __MVME_CONTEXT_LIB_H__
#define __MVME_CONTEXT_LIB_H__

#include "mvme_listfile_utils.h"
#include "mvme_context.h"
#include "analysis_service_provider.h"

// Config to file writing. No error reporting, just a boolean success/fail
// return value.
bool write_vme_config_to_file(const QString &filename, const VMEConfig *vmeConfig);
bool write_analysis_to_file(const QString &filename, const analysis::Analysis *analysis);

// Saving of vme and analysis configs. Error reporting done via QMessageBoxes.
//
// The returned boolean is true if the config was saved or the user wants to
// discard pending changes. False if the config could not be saved or the user
// wants to cancel the current action.
// The second member of the return value contains the name of the file that was
// written.

// These add vme properties from the vme config to the analysis then save the
// analysis to the user selected output file.
QPair<bool, QString> gui_save_analysis_config(analysis::Analysis *analysis,
                                        const QString &fileName,
                                        QString startPath,
                                        QString fileFilter,
                                        AnalysisServiceProvider *serviceProvider);

QPair<bool, QString> gui_save_analysis_config_as(analysis::Analysis *analysis,
                                           QString startPath,
                                           QString fileFilter,
                                           AnalysisServiceProvider *serviceProvider);

QPair<bool, QString> gui_save_vme_config(VMEConfig *vmeConfig, const QString &filename, QString startPath);
QPair<bool, QString> gui_save_vme_config_as(VMEConfig *vmeConfig, QString startPath);

// These get the VMEConfig/Analysis object from the context. If the object is
// modified the save -> saveas sequence is run. They also set the new filename
// on the context object and restart the file autosaver.
QPair<bool, QString> gui_analysis_maybe_save_if_modified(AnalysisServiceProvider *serviceProvider);
QPair<bool, QString> gui_vmeconfig_maybe_save_if_modified(AnalysisServiceProvider *serviceProvider);


/* IMPORTANT: Does not check if the current analysis is modified before loading
 * one from the listfile. Perform this check before calling this function!. */
LIBMVME_EXPORT const ListfileReplayHandle &context_open_listfile(
    MVMEContext *context,
    const QString &filename,
    OpenListfileOptions options = {});

// Returns true if the given listfile zip archive contains a file called
// "analysis.analysis".
bool LIBMVME_EXPORT
    listfile_contains_analysis(const QString &listfileArchivePath);

bool LIBMVME_EXPORT
    listfile_is_archive_corrupted(const QString &listfileArchivePath);

struct AnalysisPauser
{
    explicit AnalysisPauser(AnalysisServiceProvider *context);
    ~AnalysisPauser();

    AnalysisServiceProvider *m_serviceProvider;
    AnalysisWorkerState m_prevState;
};

void LIBMVME_EXPORT new_vme_config(MVMEContext *context);

#endif /* __MVME_CONTEXT_LIB_H__ */
