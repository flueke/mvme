#ifndef UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd
#define UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd

#include "util.h"
#include <QMetaType>
#include <QMap>
#include <QString>
#include <QDateTime>

enum class TriggerCondition
{
    NIM1,
    Periodic,
    Interrupt
};

enum class DAQState
{
    Idle,
    Starting,
    Running,
    Stopping
};

Q_DECLARE_METATYPE(DAQState);

enum class VMEModuleType
{
    Invalid = 0,
    MADC32  = 1,
    MQDC32  = 2,
    MTDC32  = 3,
    MDPP16  = 4,
    MDPP32  = 5,
    MDI2    = 6,

    //RegisterRead = 40, // TODO: add support for this

    Generic = 48,
};

static const QMap<TriggerCondition, QString> TriggerConditionNames =
{
    { TriggerCondition::NIM1,       "NIM1" },
    { TriggerCondition::Periodic,   "Periodic" },
    { TriggerCondition::Interrupt,  "Interrupt" },
};

static const QMap<VMEModuleType, QString> VMEModuleTypeNames =
{
    { VMEModuleType::MADC32,    "MADC-32" },
    { VMEModuleType::MQDC32,    "MQDC-32" },
    { VMEModuleType::MTDC32,    "MTDC-32" },
    { VMEModuleType::MDPP16,    "MDPP-16" },
    { VMEModuleType::MDPP32,    "MDPP-32" },
    { VMEModuleType::MDI2,      "MDI-2" },
    { VMEModuleType::Generic,   "Generic" },
};

static const QMap<VMEModuleType, QString> VMEModuleShortNames =
{
    { VMEModuleType::MADC32,    "madc32" },
    { VMEModuleType::MQDC32,    "mqdc32" },
    { VMEModuleType::MTDC32,    "mtdc32" },
    { VMEModuleType::MDPP16,    "mdpp16" },
    { VMEModuleType::MDPP32,    "mdpp32" },
    { VMEModuleType::MDI2,      "mdi2" },
    { VMEModuleType::Generic,   "generic" },
};

inline bool isMesytecModule(VMEModuleType type)
{
    switch (type)
    {
        case VMEModuleType::MADC32:
        case VMEModuleType::MQDC32:
        case VMEModuleType::MTDC32:
        case VMEModuleType::MDPP16:
        case VMEModuleType::MDPP32:
        case VMEModuleType::MDI2:
            return true;
        default:
            break;
    }
    return false;
}

static const u32 EndMarker = 0x87654321;
static const u32 BerrMarker = 0xffffffff;
/* Used for readout stack generation for mesytec modules. This is the number of
 * 32 bit words to transfer. Note that if the number of 16-bit words transfered
 * exceeds the VMUSBs 2k event assembly buffer it will generate event headers
 * with the Buffer::ContinuationMask bit set (Section 3.4.2 of the manual). */
static const size_t FifoReadTransferSize = 1000;
static const int RawHistogramBits = 13;
static const int RawHistogramResolution = 1 << RawHistogramBits;

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
    u64 listFileTotalBytes = 0;

    //u64 buffersProcessed = 0;
    //u64 eventsProcessed = 0;

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

enum class GlobalMode
{
    NotSet,
    DAQ,
    ListFile
};

Q_DECLARE_METATYPE(GlobalMode);

#endif
