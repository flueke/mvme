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
#ifndef __RUN_INFO_H__
#define __RUN_INFO_H__

#include <QString>
#include <QVariantMap>

/* Information about the current DAQ run or the run that's being replayed from
 * a listfile. */
struct RunInfo
{
    /* This is the full runId string. It is used to generate the listfile
     * archive name and the listfile filename inside the archive. */
    QString runId;

    /* Set to true to retain histogram contents across replays. Keeping the
     * contents only works if the number of bins and the binning do not change
     * between runs. If set to false all histograms will be cleared before the
     * replay starts. */
    // TODO: replace with flags
    bool keepAnalysisState = false;
    bool isReplay = false;
    bool ignoreStartupErrors = false;

    QVariantMap infoDict;
};

inline bool operator==(const RunInfo &a, const RunInfo &b)
{
    return a.runId == b.runId
        && a.keepAnalysisState == b.keepAnalysisState
        && a.isReplay == b.isReplay
        && a.ignoreStartupErrors == b.ignoreStartupErrors
        && a.infoDict == b.infoDict;
}

inline bool operator!=(const RunInfo &a, const RunInfo &b)
{
    return !(a == b);
}

#endif /* __RUN_INFO_H__ */
