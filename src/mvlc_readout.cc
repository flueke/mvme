#include "mvlc_readout.h"

#include <atomic>
#include <yaml-cpp/yaml.h>

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

ConnectionType connection_type_from_string(const std::string &str)
{
    if (str == listfile::get_filemagic_usb())
        return ConnectionType::USB;

    if (str == listfile::get_filemagic_eth())
        return ConnectionType::ETH;

    return {};
}
} // end anon namespace

bool CrateConfig::operator==(const CrateConfig &o) const
{
    return connectionType == o.connectionType
        && usbIndex == o.usbIndex
        && usbSerial == o.usbSerial
        && ethHost == o.ethHost
        && stacks == o.stacks
        && triggers == o.triggers;
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

    // TODO: optional init sequence

    out << YAML::EndMap; // crate

    assert(out.good());

    return out.c_str();
}

CrateConfig crate_config_from_yaml(const std::string &yamlText)
{
    CrateConfig result = {};

    YAML::Node yRoot = YAML::Load(yamlText);

    if (!yRoot || !yRoot["crate"])
        return result;

    // crate
    if (const auto &yCrate = yRoot["crate"])
    {
        if (const auto &yCon = yCrate["connection"])
        {
            result.connectionType = connection_type_from_string(yCon["type"].as<std::string>());
            result.usbIndex = yCon["usbIndex"].as<int>();
            result.usbSerial = yCon["usbSerial"].as<std::string>();
            result.ethHost = yCon["ethHost"].as<std::string>();
        }

        // stacks
        if (const auto &yStacks = yCrate["stacks"])
        {
            for (const auto &yStack: yStacks)
            {
                StackCommandBuilder stack;

                for (const auto &yGroup: yStack)
                {
                    std::string groupName = yGroup["name"].as<std::string>();
                    auto groupCommands = stack_commands_from_buffer(yGroup["contents"].as<std::vector<u32>>());
                    stack.addGroup(groupName, groupCommands);
                }

                result.stacks.emplace_back(stack);
            }
        }

        // triggers
        if (const auto &yTriggers = yCrate["triggers"])
        {
            for (const auto &yTrig: yTriggers)
                result.triggers.push_back(yTrig.as<u32>());
        }
    }

    return result;
}

} // end namespace mvlc
} // end namespace mesytec

