#include <iostream>
#include <tuple>

#include <mesytec-mvlc/mvlc_command_builders.h>
#include <mesytec-mvlc/mvlc_constants.h>
#include <mesytec-mvlc/mvlc_dialog_util.h>
#include <mesytec-mvlc/mvlc_readout.h>

#include "mvlc_stream_worker.h"
#include "vme_config.h"
#include "vme_config_scripts.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Error: no input file specified." << std::endl;
        return 1;
    }

    QString inFilename(argv[1]);

    std::unique_ptr<VMEConfig> vmeConfig;
    QString message;

    std::tie(vmeConfig, message) = read_vme_config_from_file(inFilename);

    if (!vmeConfig || !message.isEmpty())
        std::cerr << "Error loading mvme VME config: " << message.toStdString() << endl;

    mesytec::mvlc::CrateConfig dstConfig;

    auto ctrlSettings = vmeConfig->getControllerSettings();

    switch (vmeConfig->getControllerType())
    {
        case VMEControllerType::MVLC_ETH:
            dstConfig.connectionType = mesytec::mvlc::ConnectionType::ETH;
            dstConfig.ethHost = ctrlSettings["mvlc_hostname"].toString().toStdString();
            break;

        case VMEControllerType::MVLC_USB:
            dstConfig.connectionType = mesytec::mvlc::ConnectionType::USB;
            dstConfig.usbIndex = ctrlSettings.value("index", "-1").toInt();
            dstConfig.usbSerial = ctrlSettings["serial"].toString().toStdString();
            break;

        default:
            std::cerr << "Warning: mvme config does not use an MVLC VME controller."
                << " Leaving MVLC connection information empty in generated config." << std::endl;
            break;
    }

// TODO: 
// readout_stacks:
//    []
//  stack_triggers:
//    []
//  init_sequence:
//  []

    auto convert_command = [](const auto &srcCmd)
    {
        using namespace vme_script;
        using mvlcCT = mesytec::mvlc::StackCommand::CommandType;

        mesytec::mvlc::StackCommand dstCmd;

        switch (srcCmd.type)
        {
            case CommandType::Read:
                dstCmd.type = mvlcCT::VMERead;
                dstCmd.address = srcCmd.address;
                dstCmd.amod = srcCmd.addressMode;
                dstCmd.dataWidth = (srcCmd.dataWidth == DataWidth::D16 ? mesytec::mvlc::VMEDataWidth::D16 : mesytec::mvlc::VMEDataWidth::D32);
                break;

            case CommandType::Write:
            case CommandType::WriteAbs:
                dstCmd.type = mvlcCT::VMEWrite;
                dstCmd.address = srcCmd.address;
                dstCmd.value = srcCmd.value;
                dstCmd.amod = srcCmd.addressMode;
                dstCmd.dataWidth = (srcCmd.dataWidth == DataWidth::D16 ? mesytec::mvlc::VMEDataWidth::D16 : mesytec::mvlc::VMEDataWidth::D32);
                break;

            case CommandType::Wait:
                dstCmd.type = mvlcCT::SoftwareDelay;
                dstCmd.value = srcCmd.delay_ms;
                break;

            case CommandType::Marker:
                dstCmd.type = mvlcCT::WriteMarker;
                dstCmd.value = srcCmd.value;
                break;

            case CommandType::BLT:
            case CommandType::BLTFifo:
            case CommandType::MBLT:
            case CommandType::MBLTFifo:
                dstCmd.type = mvlcCT::VMERead;
                dstCmd.address = srcCmd.address;
                dstCmd.amod = srcCmd.addressMode;
                dstCmd.transfers = srcCmd.transfers;
                break;

            case CommandType::Blk2eSST64: // TODO: implement me
                throw std::runtime_error("implement me");

            case CommandType::MVLC_WriteSpecial:
                dstCmd.type = mvlcCT::WriteSpecial;
                dstCmd.value = srcCmd.value;
                break;

            default:
                break;
        }

        return dstCmd;
    };

    auto add_stack_group = [&convert_command](
        mesytec::mvlc::StackCommandBuilder &stack, const std::string &groupName,
        const vme_script::VMEScript &contents)
    {
        stack.beginGroup(groupName);

        for (const auto &srcCmd: contents)
        {
            if (auto dstCmd = convert_command(srcCmd))
                stack.addCommand(dstCmd);
        }

        return stack;
    };

    const auto eventConfigs = vmeConfig->getEventConfigs();

    for (size_t ei = 0; ei < eventConfigs.size(); ei++)
    {
        const auto eventConfig = eventConfigs[ei];
        const auto moduleConfigs = eventConfig->getModuleConfigs();

        mesytec::mvlc::StackCommandBuilder readoutStack(eventConfig->objectName().toStdString());

        add_stack_group(
            readoutStack, "readout_start",
            mesytec::mvme::parse(eventConfig->vmeScripts["readout_start"]));

        for (size_t mi = 0; mi < moduleConfigs.size(); mi++)
        {
            const auto moduleConfig = moduleConfigs[mi];

            auto moduleName = moduleConfig->objectName().toStdString();

            add_stack_group(
                readoutStack,
                moduleName,
                mesytec::mvme::parse(
                    moduleConfig->getReadoutScript(), moduleConfig->getBaseAddress()));
        }

        add_stack_group(
            readoutStack, "readout_end",
            mesytec::mvme::parse(eventConfig->vmeScripts["readout_end"]));

        dstConfig.stacks.emplace_back(readoutStack);
    }

    for (size_t ei = 0; ei < eventConfigs.size(); ei++)
    {
        const auto eventConfig = eventConfigs[ei];

        using namespace mesytec::mvlc;

        switch (eventConfig->triggerCondition)
        {
            case TriggerCondition::Interrupt:
                {
                    auto triggerType = (eventConfig->triggerOptions["IRQUseIACK"].toBool()
                                        ? stacks::IRQWithIACK
                                        : stacks::IRQNoIACK);
                    dstConfig.triggers.push_back(trigger_value(triggerType, eventConfig->irqLevel));
                }
                break;

            case TriggerCondition::TriggerIO:
                dstConfig.triggers.push_back(trigger_value(stacks::External));
                break;

            default:
                std::cerr << "Warning: unhandled trigger type for event '"
                    << eventConfig->objectName().toStdString()
                    << "'. Defaulting to 'TriggerIO'."
                    << std::endl;
                dstConfig.triggers.push_back(trigger_value(stacks::External));
                break;
        }
    }

    std::cout << mesytec::mvlc::to_yaml(dstConfig) << std::endl;

    return 0;
}
