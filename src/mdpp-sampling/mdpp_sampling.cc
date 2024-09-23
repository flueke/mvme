
#include "mdpp_sampling.h"

#include <mesytec-mvlc/util/logging.h>

#include "run_info.h"
#include "vme_config.h"

using namespace mesytec::mvlc;

namespace mesytec::mvme
{

struct MdppSamplingConsumer::Private
{
    std::shared_ptr<spdlog::logger> logger_;
    Logger qtLogger_;

    RunInfo runInfo_;
    const VMEConfig *vmeConfig_;
    analysis::Analysis *analysis_;

    bool hasModuleInterest(s32 crateIndex, s32 eventIndex, s32 moduleIndex)
    {
        return true;
    };
};

MdppSamplingConsumer::MdppSamplingConsumer(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->logger_ = get_logger("MdppSamplingConsumer");
    d->logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [tid%t] %v");
    d->logger_->set_level(spdlog::level::debug);
}

MdppSamplingConsumer::~MdppSamplingConsumer()
{
}

void MdppSamplingConsumer::setLogger(Logger logger)
{
    d->qtLogger_ = logger;
}

StreamConsumerBase::Logger &MdppSamplingConsumer::getLogger()
{
    return d->qtLogger_;
}

void MdppSamplingConsumer::beginRun(
    const RunInfo &runInfo, const VMEConfig *vmeConfig, analysis::Analysis *analysis)
{
    d->runInfo_ = runInfo;
    d->vmeConfig_ = vmeConfig;
    d->analysis_ = analysis;
}

void MdppSamplingConsumer::endRun(const DAQStats &stats, const std::exception *e)
{
}

void MdppSamplingConsumer::beginEvent(s32 eventIndex)
{
    (void) eventIndex;
}

void MdppSamplingConsumer::endEvent(s32 eventIndex)
{
    (void) eventIndex;
}

void MdppSamplingConsumer::processModuleData(
    s32 crateIndex, s32 eventIndex, const ModuleData *moduleDataList, unsigned moduleCount)
{
    for (unsigned moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex)
    {
        if (d->hasModuleInterest(crateIndex, eventIndex, moduleIndex))
        {
            auto dataBlock = moduleDataList[moduleIndex].data;
            std::vector<u32> buffer(dataBlock.size);
            std::copy(dataBlock.data, dataBlock.data+dataBlock.size, std::begin(buffer));
            emit moduleDataReady(crateIndex, eventIndex, moduleIndex, buffer);
        }
    }
}

void MdppSamplingConsumer::processSystemEvent(s32 crateIndex, const u32 *header, u32 size)
{
    (void) crateIndex;
    (void) header;
    (void) size;
}

void MdppSamplingConsumer::processModuleData(s32 eventIndex, s32 moduleIndex, const u32 *data, u32 size)
{
    (void) eventIndex;
    (void) moduleIndex;
    (void) data;
    (void) size;
    assert(!"don't call me please!");
    throw std::runtime_error(fmt::format("{}: don't call me please!", __PRETTY_FUNCTION__));
}

struct MdppSamplingUi::Private
{
};

MdppSamplingUi::MdppSamplingUi(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{ }

MdppSamplingUi::~MdppSamplingUi() { }

void MdppSamplingUi::handleModuleData(s32 crateIndex, s32 eventIndex, s32 moduleIndex, const std::vector<u32> buffer)
{
}

}
