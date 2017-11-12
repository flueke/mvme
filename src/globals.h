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
#ifndef UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd
#define UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd

#include "util.h"
#include <QMetaType>
#include <QMap>
#include <QString>
#include <QDateTime>

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
        // TODO: SIS3153 has Timer1 and Timer2
        // But Timer2 might have to be used as a watchdog...
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

enum class EventProcessorState
{
    Idle,
    Running
};

Q_DECLARE_METATYPE(EventProcessorState);

enum class GlobalMode
{
    NotSet,
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

static const u32 EndMarker = 0x87654321;
static const u32 BerrMarker = 0xffffffff;
static const int MaxVMEEvents  = 12;
static const int MaxVMEModules = 20;

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

    inline void addBytesRead(u64 count)
    {
        totalBytesRead += count;
    }

    inline void addBuffersRead(u64 count)
    {
        totalBuffersRead += count;
    }

    QDateTime startTime;
    QDateTime endTime;

    u64 totalBytesRead = 0;     // bytes read from the controller including protocol overhead
    u64 totalBuffersRead = 0;   // number of buffers received from the controller
    u64 buffersWithErrors = 0;  // buffers for which processing did not succeeed (structure not intact, etc)
    u64 droppedBuffers = 0;     // number of buffers not passed to the analysis
    //u64 totalEventsRead = 0;    // FIXME: count controller side events here ("triggers / subevents?")
    u64 totalNetBytesRead = 0;  // The number of bytes read excluding protocol
                                // overhead. This should be a measure for the
                                // amount of data the VME bus transferred.

    u64 listFileBytesWritten = 0;
    u64 listFileTotalBytes = 0; // For replay mode: the size of the replay file
    QString listfileFilename;

    //u64 mvmeBuffersSeen = 0;
    //u64 mvmeBuffersWithErrors = 0;

    using ModuleCounters = std::array<u32, MaxVMEModules>;
    std::array<ModuleCounters, MaxVMEEvents> moduleCounters;
    std::array<u32, MaxVMEEvents> eventCounters;
};

/* Information about the current DAQ run or the run that's being replayed from
 * a listfile. */
struct RunInfo
{
    QString runId;

    /* Set to true to retain histogram contents across replays. Keeping the
     * contents only works if the number of bins and the binning do not change
     * between runs. If set to false all histograms will be cleared before the
     * replay starts. */
    bool keepAnalysisState = false;
};

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
    bool enabled;               // true if a listfile should be written
    ListFileFormat format;      // the format to write
    QString directory;          // Path to the output directory. If it's not a
                                // full path it's relative to the workspace directory.
    QString fullDirectory;      // Always the full path to the listfile output directory.
    int compressionLevel;       // zlib compression level
};

#endif
