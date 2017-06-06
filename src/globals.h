#ifndef UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd
#define UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd

#include "util.h"
#include <QMetaType>
#include <QMap>
#include <QString>
#include <QDateTime>

// IMPORTANT: The numeric values are stored in VME config files. Only insert
// new entries at the bottom!
enum class TriggerCondition
{
    NIM1,
    Periodic,
    Interrupt,
    // TODO: add Disabled
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

struct DAQStats
{
    void start()
    {
        startTime = QDateTime::currentDateTime();
        intervalUpdateTime.restart();
    }

    void stop()
    {
        endTime = QDateTime::currentDateTime();
    }

    void addBytesRead(u64 count)
    {
        totalBytesRead += count;
        intervalBytesRead += count;
        maybeUpdateIntervalCounters();
    }

    void addBuffersRead(u64 count)
    {
        totalBuffersRead += count;
        intervalBuffersRead += count;
        maybeUpdateIntervalCounters();
    }

    void addEventsRead(u64 count)
    {
        totalEventsRead += count;
        intervalEventsRead += count;
        maybeUpdateIntervalCounters();
    }

    void maybeUpdateIntervalCounters()
    {
        int msecs = intervalUpdateTime.elapsed();
        if (msecs >= 1000)
        {
            double seconds = msecs / 1000.0;
            bytesPerSecond = intervalBytesRead / seconds;
            buffersPerSecond = intervalBuffersRead / seconds;
            eventsPerSecond = intervalEventsRead / seconds;
            intervalBytesRead = 0;
            intervalBuffersRead = 0;
            intervalEventsRead = 0;
            intervalUpdateTime.restart();
        }
    }

    QDateTime startTime;
    QDateTime endTime;

    u64 totalBytesRead = 0;
    u64 totalBuffersRead = 0;
    u64 buffersWithErrors = 0;
    u64 droppedBuffers = 0;
    u64 totalEventsRead = 0;

    QTime intervalUpdateTime;
    u64 intervalBytesRead = 0;
    u64 intervalBuffersRead = 0;
    u64 intervalEventsRead = 0;

    double bytesPerSecond = 0.0;
    double buffersPerSecond = 0.0;
    double eventsPerSecond = 0.0;


    u32 vmusbAvgEventsPerBuffer = 0;

    u32 avgEventsPerBuffer = 0;
    u32 avgReadSize = 0;

    int freeBuffers = 0;

    u64 listFileBytesWritten = 0;
    u64 listFileTotalBytes = 0; // For replay mode: the size of the replay file
    QString listfileFilename;

    u64 totalBuffersProcessed = 0;
    //u64 eventsProcessed = 0;
    u64 mvmeBuffersSeen = 0;
    u64 mvmeBuffersWithErrors = 0;

    struct EventCounters
    {
        u64 events = 0;
        u64 headerWords = 0;
        u64 dataWords = 0;
        u64 eoeWords = 0;
    };

    // maps EventConfig/ModuleConfig to EventCounters
    QHash<QObject *, EventCounters> eventCounters;
};

/* Information about the current DAQ run or the run that's being replayed from
 * a listfile. */
struct RunInfo
{
    QString runId;
};

#endif
