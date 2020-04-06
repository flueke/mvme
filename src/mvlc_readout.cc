#include "mvlc_readout.h"

#include <atomic>
#include <yaml-cpp/yaml.h>

#include "mvlc_listfile.h"
#include "yaml-cpp/emittermanip.h"

namespace mesytec
{
namespace mvlc
{

#if 0
struct ReadoutWorker::Private
{
    Private(MVLC &mvlc_)
        : mvlc(mvlc_)
    {}

    std::atomic<ReadoutWorker::State> state;
    std::atomic<ReadoutWorker::State> desiredState;
    MVLC mvlc;
    std::vector<StackTrigger> stackTriggers;
};

ReadoutWorker::ReadoutWorker(MVLC &mvlc, const std::vector<StackTrigger> &stackTriggers)
    : d(std::make_unique<Private>(mvlc))
{
    d->state = State::Idle;
    d->desiredState = State::Idle;
    d->stackTriggers = stackTriggers;
}

ReadoutWorker::State ReadoutWorker::state() const
{
    return d->state;
}

std::error_code ReadoutWorker::start(const std::chrono::seconds &duration)
{
}

std::error_code ReadoutWorker::stop()
{
}

std::error_code ReadoutWorker::pause()
{
}

std::error_code ReadoutWorker::resume()
{
}
#endif

namespace
{
std::string to_string(ConnectionType ct)
{
    switch (ct)
    {
        case ConnectionType::USB:
            return listfile::get_filemagic_usb();

        case ConnectionType::ETH:
            return listfile::get_filemagic_eth();
    }

    return {};
}
}

inline void emit_stack(YAML::Emitter &out, const StackCommandBuilder &stack)
{
    out << YAML::BeginSeq;

    for (const auto &group: stack.getGroups())
    {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << group.name;
        out << YAML::Key << "contents" << YAML::Value << make_stack_buffer(group.commands);
        out << YAML::EndMap;
    }

    out << YAML::EndSeq;
}

std::string to_yaml(const CrateConfig &crateConfig)
{
    YAML::Emitter out;

    out << YAML::Hex;

    assert(out.good());

    out << YAML::BeginMap;
    out << YAML::Key << "crate" << YAML::Value << YAML::BeginMap;

    out << YAML::Key << "connection" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "type" << YAML::Value << to_string(crateConfig.connectionType);
    out << YAML::Key << "usbIndex" << YAML::Value << crateConfig.usbIndex;
    out << YAML::Key << "usbSerial" << YAML::Value << crateConfig.usbSerial;
    out << YAML::Key << "ethHost" << YAML::Value << crateConfig.ethHost;
    out << YAML::EndMap; // connection

    out << YAML::Key << "stacks" << YAML::Value << YAML::BeginSeq;
    for (const auto &stack: crateConfig.stacks)
    {
        emit_stack(out, stack);
    }
    out << YAML::EndSeq; // stacks

    out << YAML::Key << "triggers" << YAML::Value << crateConfig.triggers;

    out << YAML::EndMap; // crate

    assert(out.good());

    return out.c_str();
}

CrateConfig from_yaml(const std::string &yaml);

} // end namespace mvlc
} // end namespace mesytec

