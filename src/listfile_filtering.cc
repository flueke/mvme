#include "listfile_filtering.h"

#include <mesytec-mvlc/util/logging.h>
#include <QDebug>

#include "run_info.h"

using namespace mesytec::mvlc;

namespace
{
    // https://stackoverflow.com/a/1759114/17562886
    template <class NonMap>
    struct Print
    {
        static void print(const QString &tabs, const NonMap &value)
        {
            qDebug() << tabs << value;
        }
    };

    template <class Key, class ValueType>
    struct Print<class QMap<Key, ValueType>>
    {
        static void print(const QString &tabs, const QMap<Key, ValueType> &map)
        {
            const QString extraTab = tabs + "\t";
            QMapIterator<Key, ValueType> iterator(map);
            while (iterator.hasNext())
            {
                iterator.next();
                qDebug() << tabs << iterator.key();
                Print<ValueType>::print(extraTab, iterator.value());
            }
        }
    };

    template <class Type>
    void printMe(const Type &type)
    {
        Print<Type>::print("", type);
    };
}

struct ListfileFilterStreamConsumer::Private
{
    Logger logger_;
};

ListfileFilterStreamConsumer::ListfileFilterStreamConsumer()
    : d(std::make_unique<Private>())
{
    spdlog::info("{} => {}", __PRETTY_FUNCTION__, fmt::ptr(this));
}

ListfileFilterStreamConsumer::~ListfileFilterStreamConsumer()
{
    spdlog::info("{} destroying {}", __PRETTY_FUNCTION__, fmt::ptr(this));
}

void ListfileFilterStreamConsumer::setLogger(Logger logger)
{
    d->logger_ = logger;
}

#if 0
[2023-01-30 16:43:07.256] [info] virtual void ListfileFilterStreamConsumer::beginRun(const RunInfo&, const VMEConfig*, const analysis::Analysis*) @ 0x555556868390
"" "ExperimentName"
"\t" QVariant(QString, "Experiment")
"" "ExperimentTitle"
"\t" QVariant(QString, "")
"" "MVMEWorkspace"
"\t" QVariant(QString, "/home/florian/Documents/mvme-workspaces-on-hdd/2301_listfile-filtering")
"" "replaySourceFile"
"\t" QVariant(QString, "00-mdpp_trig.zip")

runId is "00-mdpp_trig"
#endif

void ListfileFilterStreamConsumer::beginRun(const RunInfo &runInfo, const VMEConfig *vmeConfig, const analysis::Analysis *analysis)
{
    spdlog::info("{} @ {}", __PRETTY_FUNCTION__, fmt::ptr(this));
    qDebug() << runInfo.runId;
    printMe(runInfo.infoDict);
}

void ListfileFilterStreamConsumer::endRun(const DAQStats &stats, const std::exception *e)
{
}

void ListfileFilterStreamConsumer::beginEvent(s32 eventIndex)
{
}

void ListfileFilterStreamConsumer::processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size)
{
}

void ListfileFilterStreamConsumer::endEvent(s32 eventIndex)
{
}

void ListfileFilterStreamConsumer::processSystemEvent(s32 crateIndex, const u32 *header, u32 size)
{
}