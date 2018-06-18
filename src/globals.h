/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd
#define UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd

#include "util.h"
#include <QMetaType>
#include <QMap>
#include <QString>
#include <QDateTime>

#include "vme_config_limits.h"

/* IMPORTANT: The numeric values of this enum where stored in the VME config
 * files prior to version 3. To make conversion from older config versions
 * possible do not change the order of the enum! */
enum class TriggerCondition
{
    NIM1,               // VMUSB
    Periodic,           // VMUSB and SIS3153
    Interrupt,          // VMUSB and SIS3153
    Input1RisingEdge,   // SIS3153
    Input1FallingEdge,  // SIS3153
    Input2RisingEdge,   // SIS3153
    Input2FallingEdge   // SIS3153
};

enum class DAQState
{
    Idle,
    Starting,
    Running,
    Stopping,
    Paused
};

Q_DECLARE_METATYPE(DAQState);

enum class GlobalMode
{
    DAQ,
    ListFile
};

Q_DECLARE_METATYPE(GlobalMode);

static const QMap<TriggerCondition, QString> TriggerConditionNames =
{
    { TriggerCondition::NIM1,       "NIM1" },
    { TriggerCondition::Periodic,   "Periodic" },
    { TriggerCondition::Interrupt,  "Interrupt" },
    { TriggerCondition::Input1RisingEdge,   "Input 1 Rising Edge" },
    { TriggerCondition::Input1FallingEdge,  "Input 1 Falling Edge" },
    { TriggerCondition::Input2RisingEdge,   "Input 2 Rising Edge" },
    { TriggerCondition::Input2FallingEdge,  "Input 2 Falling Edge" },
};

static const QMap<DAQState, QString> DAQStateStrings =
{
    { DAQState::Idle,       QSL("Idle") },
    { DAQState::Starting,   QSL("Starting") },
    { DAQState::Running,    QSL("Running") },
    { DAQState::Stopping,   QSL("Stopping") },
    { DAQState::Paused,     QSL("Paused") },
};

static const u32 EndMarker     = 0x87654321u;
static const u32 BerrMarker    = 0xffffffffu;

struct DAQStats
{
    inline void start()
    {
        startTime = QDateTime::currentDateTime();
    }

    inline void stop()
    {
        endTime = QDateTime::currentDateTime();
    }

    QDateTime startTime;
    QDateTime endTime;

    u64 totalBytesRead = 0;     // bytes read from the controller including protocol overhead
    u64 totalBuffersRead = 0;   // number of buffers received from the
                                // controller. This includes buffers that can
                                // later on lead to a parse error, thus it does
                                // not represent the number of "good" buffers.
    u64 buffersWithErrors = 0;  // buffers for which processing did not succeeed (structure not intact, etc)
    u64 droppedBuffers = 0;     // number of buffers not passed to the analysis due to the queue being full
    u64 totalNetBytesRead = 0;  // The number of bytes read excluding protocol
                                // overhead. This should be a measure for the
                                // amount of data the VME bus transferred.

    u64 listFileBytesWritten = 0;
    u64 listFileTotalBytes = 0; // For replay mode: the size of the replay file
    QString listfileFilename; // For replay mode: the current replay filename

    u64 getAnalyzedBuffers() const { return totalBuffersRead - droppedBuffers; }
    double getAnalysisEfficiency() const;
};

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
    //bool generateExportFiles = false;

};

inline bool operator==(const RunInfo &a, const RunInfo &b)
{
    return a.runId == b.runId
        && a.keepAnalysisState == b.keepAnalysisState
        && a.isReplay == b.isReplay;
}

inline bool operator!=(const RunInfo &a, const RunInfo &b)
{
    return !(a == b);
}

enum class ListFileFormat
{
    Invalid,
    Plain,
    ZIP
};

QString toString(const ListFileFormat &fmt);
ListFileFormat fromString(const QString &str);

struct ListFileOutputInfo
{
    // Flags available for the flags member below.
    static const u32 UseRunNumber = 1u << 0;
    static const u32 UseTimestamp = 1u << 1;

    // TODO: move enabled into flags
    bool enabled = true;        // true if a listfile should be written

    ListFileFormat format;      // the format to write

    QString directory;          // Path to the output directory. If it's not a
                                // full path it's relative to the workspace directory.
                                //
    QString fullDirectory;      // Always the full path to the listfile output directory.
                                // This is transient and not stored in the workspace settings.

    int compressionLevel = 1;   // zlib compression level

    QString prefix = QSL("mvmelst");

    u32 runNumber = 1;          // Incremented on endRun and if output filename already exists.

    u32 flags = UseTimestamp;
};

// without extensions
QString generate_output_basename(const ListFileOutputInfo &info);
// with .zip or .mvmelst extension
QString generate_output_filename(const ListFileOutputInfo &info);

#endif
