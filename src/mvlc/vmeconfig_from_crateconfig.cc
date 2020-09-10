#include <mesytec-mvlc/mesytec-mvlc.h>
#include "mesytec-mvlc/mvlc_command_builders.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include "mesytec-mvlc/vme_constants.h"
#include "mvlc/vmeconfig_from_crateconfig.h"
#include "vme.h"
#include "vme_script.h"

namespace mesytec
{
namespace mvme
{

// Converts a mvlc::StackCommand to a vme_script::Command. If the conversion
// cannot be performed, e.g. for StackStart or StackEnd, an invalid
// vme_script::Command is returned.
vme_script::Command stack_command_to_vmescript_command(const mvlc::StackCommand &srcCmd)
{
    using namespace vme_script;
    using mvlcCT = mesytec::mvlc::StackCommand::CommandType;

    Command dstCmd;

    switch (srcCmd.type)
    {
        case mvlcCT::VMERead:
            if (mvlc::vme_amods::is_blt_mode(srcCmd.amod))
            {
                dstCmd.type = CommandType::BLTFifo;
                dstCmd.transfers = srcCmd.transfers;
                dstCmd.addressMode = vme_address_modes::BLT32;
            }
            else if (mvlc::vme_amods::is_mblt_mode(srcCmd.amod))
            {
                dstCmd.type = CommandType::MBLTFifo;
                dstCmd.transfers = srcCmd.transfers;
                dstCmd.addressMode = vme_address_modes::MBLT64;
            }
            else if (mvlc::vme_amods::is_esst64_mode(srcCmd.amod))
            {
#warning "Implement eSST64 in VMEScript"
                break;
            }
            else // non-block reads
            {
                dstCmd.type = CommandType::Read;
                dstCmd.dataWidth = (srcCmd.dataWidth == mesytec::mvlc::VMEDataWidth::D16
                                    ? DataWidth::D16
                                    : DataWidth::D32);
                dstCmd.addressMode = srcCmd.amod;
            }

            dstCmd.address = srcCmd.address;
            break;

        case mvlcCT::VMEWrite:
            dstCmd.type = CommandType::Write;
            dstCmd.address = srcCmd.address;
            dstCmd.value = srcCmd.value;
            dstCmd.addressMode = srcCmd.amod;
            dstCmd.dataWidth = (srcCmd.dataWidth == mesytec::mvlc::VMEDataWidth::D16
                                ? DataWidth::D16
                                : DataWidth::D32);
            break;

        case mvlcCT::WriteMarker:
            dstCmd.type = CommandType::Marker;
            dstCmd.value = srcCmd.value;
            break;

        case mvlcCT::WriteSpecial:
            dstCmd.type = CommandType::MVLC_WriteSpecial;
            dstCmd.value = srcCmd.value;
            break;

        case mvlcCT::SoftwareDelay:
            dstCmd.type = CommandType::Wait;
            dstCmd.delay_ms = srcCmd.value;
            break;

        default:
            break;
    }

    return dstCmd;
}

vme_script::VMEScript command_group_to_vmescript(const mvlc::StackCommandBuilder::Group &group)
{
    vme_script::VMEScript result;

    for (const auto &cmd: group.commands)
    {
        auto vmeScriptCmd = stack_command_to_vmescript_command(cmd);

        if (is_valid(vmeScriptCmd))
            result.push_back(vmeScriptCmd);
    }

    return result;
}

QStringList command_group_to_vmescript_lines(const mvlc::StackCommandBuilder::Group &group)
{
    QStringList lines;

    lines << QSL("# ") + QString::fromStdString(group.name);

    for (const auto &cmd: command_group_to_vmescript(group))
        lines << to_string(cmd);

    return lines;
}

QString command_group_to_vmescript_string(const mvlc::StackCommandBuilder::Group &group)
{
    return command_group_to_vmescript_lines(group).join("\n");
}

QStringList command_builder_to_vmescript_lines(const mvlc::StackCommandBuilder &scb)
{
    QStringList lines;

    for (const auto &group: scb.getGroups())
    {
        lines << command_group_to_vmescript_lines(group) << "";
    }

    return lines;
}

QString command_builder_to_vmescript_string(const mvlc::StackCommandBuilder &scb)
{
    return command_builder_to_vmescript_lines(scb).join("\n");
}

std::unique_ptr<VMEConfig> vmeconfig_from_crateconfig(
    const mvlc::CrateConfig &crateConfig)
{
    auto result = std::make_unique<VMEConfig>();

    //
    // VME Controller config
    //
    VMEControllerType controllerType = {};
    QVariantMap controllerSettings;

    switch (crateConfig.connectionType)
    {
        case mvlc::ConnectionType::ETH:
            controllerType = VMEControllerType::MVLC_ETH;
            controllerSettings["mvlc_hostname"] = QString::fromStdString(crateConfig.ethHost);
            controllerSettings["mvlc_eth_enable_jumbos"] = crateConfig.ethJumboEnable;
            break;

        case mvlc::ConnectionType::USB:
            controllerType = VMEControllerType::MVLC_USB;

            if (crateConfig.usbIndex >= 0)
            {
                controllerSettings["method"] = QSL("by_index");
                controllerSettings["index"] = crateConfig.usbIndex;
            }
            else if (!crateConfig.usbSerial.empty())
            {
                controllerSettings["method"] = QSL("by_serial");
                controllerSettings["serial"] = QString::fromStdString(crateConfig.usbSerial);
            }
            break;
    }

    result->setVMEController(controllerType, controllerSettings);

    // Create two VMEScriptConfig objects, one for the initCommands and another
    // for the stopCommands. Then add the configs below the appropriate global
    // containers in the VME tree.
    {
        auto startScript = std::make_unique<VMEScriptConfig>();
        startScript->setScriptContents(
            command_builder_to_vmescript_lines(crateConfig.initCommands).join("\n"));
        startScript->setObjectName(QSL("initCommands"));

        result->addGlobalScript(startScript.release(), QSL("daq_start"));
    }

    {
        auto stopScript = std::make_unique<VMEScriptConfig>();
        stopScript->setScriptContents(
            command_builder_to_vmescript_lines(crateConfig.stopCommands).join("\n"));
        stopScript->setObjectName(QSL("stopCommands"));

        result->addGlobalScript(stopScript.release(), QSL("daq_stop"));
    }

    // Replace the Trigger/IO Script contents which should have been created as
    // a side effect of setVMEController() above.
    if (auto triggerIOScript = qobject_cast<VMEScriptConfig *>(
            result->getGlobalObjectRoot().getChild("mvlc_trigger_io")))
    {
        auto lines = command_builder_to_vmescript_lines(crateConfig.initTriggerIO);

        // Add an empty meta_block so that mvme starts the trigger_io GUI when
        // editing the script.
        lines << "" <<  QSL("meta_block_begin mvlc_trigger_io") << QSL("meta_block_end");

        triggerIOScript->setScriptContents(lines.join("\n"));
    }

    auto make_scriptconfig_from_stack_group = [] (
        const mvlc::StackCommandBuilder::Group &group) -> std::unique_ptr<VMEScriptConfig>
    {
        auto result = std::make_unique<VMEScriptConfig>();
        result->setObjectName(QString::fromStdString(group.name));
        result->setScriptContents(command_group_to_vmescript_lines(group).join("\n"));
        return result;
    };

    // For each CrateConfig.stacks (the readout stacks) create an EventConfig.
    // Use CrateConfig.triggers to set the correct mvme trigger type.
    // For each of the groups in each readout stack: (if the group contains a
    // read command) create a ModuleConfig and add it to the EventConfig.
    for (size_t stackIndex = 0; stackIndex < crateConfig.stacks.size(); stackIndex++)
    {
        try
        {
            const auto &readoutStack = crateConfig.stacks.at(stackIndex);
            auto triggerInfo = mvlc::decode_trigger_value(crateConfig.triggers.at(stackIndex));

            auto eventConfig = std::make_unique<EventConfig>();
            eventConfig->setObjectName(QString::fromStdString(readoutStack.getName()));

            if (triggerInfo.first == mvlc::stacks::TriggerType::IRQWithIACK
                || triggerInfo.first == mvlc::stacks::TriggerType::IRQNoIACK)
            {
                eventConfig->triggerCondition = TriggerCondition::Interrupt;
                eventConfig->triggerOptions[QSL("IRQUseIACK")] =
                    (triggerInfo.first == mvlc::stacks::TriggerType::IRQWithIACK);
                eventConfig->irqLevel = triggerInfo.second;
            }
            else if (triggerInfo.first == mvlc::stacks::TriggerType::External)
            {
                eventConfig->triggerCondition = TriggerCondition::TriggerIO;
            }

            // Walk the readoutStack to fill the EventConfig and create child
            // ModuleConfigs.
            //
            //
            // The first group that does not contain any
            // read commands is used as the events "readout_start" script. The
            // last group without any read commands is used as the
            // "readout_end" script. The groups in-between are each used to
            // fill the readout script of a new ModuleConfig which is added to the event.
            const auto &readoutGroups = readoutStack.getGroups();
            auto groupsIt = readoutGroups.begin();
            const auto groupsEnd = readoutGroups.end();

            // If the first group does not produce output it is used as the
            // EventConfigs "readout_start" script.
            if (groupsIt != groupsEnd && !produces_output(*groupsIt))
            {
                eventConfig->vmeScripts[QSL("readout_start")] = make_scriptconfig_from_stack_group(
                    *groupsIt).release();
                ++groupsIt;
            }

            for (; groupsIt != groupsEnd; ++groupsIt)
            {
                if (groupsIt == groupsEnd - 1 && !produces_output(*groupsIt))
                    break;

                auto moduleConfig = std::make_unique<ModuleConfig>();
                moduleConfig->setObjectName(QString::fromStdString(groupsIt->name));
                moduleConfig->getReadoutScript()->setScriptContents(
                    command_group_to_vmescript_lines(*groupsIt).join("\n"));
                moduleConfig->getReadoutScript()->setObjectName(moduleConfig->objectName());

                eventConfig->addModuleConfig(moduleConfig.release());
            }

            if (groupsIt != groupsEnd)
            {
                assert(!produces_output(*groupsIt));

                eventConfig->vmeScripts[QSL("readout_end")] = make_scriptconfig_from_stack_group(
                    *groupsIt).release();

                ++groupsIt;
            }

            assert(groupsIt == groupsEnd);

            // Add the event to the VMEConfig
            result->addEventConfig(eventConfig.release());
        }
        catch (const std::out_of_range &)
        {
            break;
        }
    }

    return result;
}

}
}
