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
#ifndef UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd
#define UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd

#include <QMetaType>
#include <QMap>
#include <QString>
#include <QDateTime>

#include <chrono>

#include "util.h"
#include "run_info.h"
#include "vme_config_limits.h"

enum class GlobalMode
{
    DAQ,
    ListFile,
    Replay = ListFile,
};

enum class DAQState
{
    Idle,
    Starting,
    Running,
    Stopping,
    Paused
};

enum class AnalysisWorkerState
{
    Idle,
    Running,
    Paused,
    SingleStepping,
};

/*
     * Combined system state depdending on the readout and analysis side states:
 *
 * DAQ / Analysis | Idle        Running Paused  SingleStepping
 * ---------------+-------------------------------------------
 * Idle           | Idle        Running Running Running
 * Starting       | Starting    Running Running Running
 * Running        | Running     Running Running Running
 * Stopping       | Stopping    Running Running Running
 * Paused         | Running     Running Running Running
*/
enum class MVMEState
{
    Idle,
    Starting,
    Running,
    Stopping,
};

Q_DECLARE_METATYPE(GlobalMode);
Q_DECLARE_METATYPE(DAQState);
Q_DECLARE_METATYPE(AnalysisWorkerState);
Q_DECLARE_METATYPE(MVMEState);

QString to_string(const AnalysisWorkerState &state);
QString to_string(const MVMEState &state);

/* IMPORTANT: The numeric values of this enum where stored in the VME config
 * files prior to version 3. To make conversion from older config versions
 * possible do not change the order of the enum! */
enum class TriggerCondition
{
    NIM1,               // VMUSB
    Periodic,           // MVLC, VMUSB and SIS3153
    Interrupt,          // MVLC, VMUSB and SIS3153
    Input1RisingEdge,   // SIS3153
    Input1FallingEdge,  // SIS3153
    Input2RisingEdge,   // SIS3153
    Input2FallingEdge,  // SIS3153
    TriggerIO,          // MVLC via the Trigger I/O logic
};

static const QMap<TriggerCondition, QString> TriggerConditionNames =
{
    { TriggerCondition::NIM1,               "NIM1" },
    { TriggerCondition::Periodic,           "Periodic" },
    { TriggerCondition::Interrupt,          "Interrupt" },
    { TriggerCondition::Input1RisingEdge,   "Input 1 Rising Edge" },
    { TriggerCondition::Input1FallingEdge,  "Input 1 Falling Edge" },
    { TriggerCondition::Input2RisingEdge,   "Input 2 Rising Edge" },
    { TriggerCondition::Input2FallingEdge,  "Input 2 Falling Edge" },
    { TriggerCondition::TriggerIO,          "MVLC Trigger I/O" },
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
        totalBytesRead = 0;
        totalBuffersRead = 0;
        buffersWithErrors = 0;
        droppedBuffers = 0;
        buffersFlushed = 0;
        listFileBytesWritten = 0;
        listFileTotalBytes = 0;
        startTime = QDateTime::currentDateTime();
        endTime = {};
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
    u64 buffersFlushed = 0;     // Number of buffers flushed to the output queue (from readout or replay to the analysis side).
    u64 listFileBytesWritten = 0;
    u64 listFileTotalBytes = 0; // For replay mode: the size of the replay file
    QString listfileFilename; // For replay mode: the current replay filename

    u64 getAnalyzedBuffers() const { return totalBuffersRead - droppedBuffers; }
    double getAnalysisEfficiency() const;
};

enum class ListFileFormat
{
    Invalid,
    Plain,
    ZIP,
    LZ4,
    ZMQ_Ganil,
};

QString toString(const ListFileFormat &fmt);
ListFileFormat listFileFormat_fromString(const QString &str);

struct ListFileOutputInfo
{
    // Flags available for the flags member below.
    static const u32 UseRunNumber = 1u << 0;
    static const u32 UseTimestamp = 1u << 1;
    static const u32 SplitBySize  = 1u << 2;
    static const u32 SplitByTime  = 1u << 3;
    static const u32 UseFormatStr = 1u << 4;

    // TODO: maybe move enabled into flags
    bool enabled = true;        // true if a listfile should be written

    ListFileFormat format;      // the format to write

    QString directory;          // Path to the output directory. If it's not a
                                // full path it's relative to the workspace directory.
                                //
    QString fullDirectory;      // Always the full path to the listfile output directory.
                                // This is transient and not stored in the workspace settings.

    int compressionLevel = 1;   // zlib/lz4 compression level

    QString prefix = QSL("mvmelst");
    QString suffix;
    QString fmtStr = QSL("mvmelst_run{0:03d}");

    u32 runNumber = 1;          // Incremented on endRun and if output filename already exists.

    u32 flags = UseTimestamp;

    size_t splitSize = Gigabytes(1);
    std::chrono::seconds splitTime = std::chrono::seconds(3600);
};

// listfile name without filename extensions
// Can throw fmt::format_error
QString generate_output_basename(const ListFileOutputInfo &info);

// same as above but with .zip or .mvmelst extension
QString generate_output_filename(const ListFileOutputInfo &info);

void write_listfile_output_info_to_qsettings(const ListFileOutputInfo &info, QSettings &settings);
ListFileOutputInfo read_listfile_output_info_from_qsettings(QSettings &settings);

QVariant listfile_output_info_to_variant(const ListFileOutputInfo &info);
ListFileOutputInfo listfile_output_info_from_variant(const QVariant &var);

enum class ControllerState
{
    Disconnected,
    Connecting,
    Connected,
};

Q_DECLARE_METATYPE(ControllerState);

enum class ListfileBufferFormat
{
    MVMELST,      // .mvmelst style formatted data. Produced by VMUSB and SIS readouts
    MVLC_ETH,     // MVLC_ETH UDP packet data containing including the two sepcial header words
    MVLC_USB,     // MVLC_USB buffers. Do not contain any additional header words.
};

const char *to_string(const ListfileBufferFormat &fmt);

#endif
