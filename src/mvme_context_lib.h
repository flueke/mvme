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

// Saving of vme and analysis configs

QPair<bool, QString> gui_saveAnalysisConfig(analysis::Analysis *analysis_ng,
                                        const QString &fileName,
                                        QString startPath,
                                        QString fileFilter);

QPair<bool, QString> gui_saveAnalysisConfigAs(analysis::Analysis *analysis_ng,
                                          QString startPath,
                                          QString fileFilter);



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
