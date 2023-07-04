#include <mesytec-mvlc/mesytec-mvlc.h>
#include <QCoreApplication>

#include "mvlc_daq.h"
#include "mvlc_trigger_io_script.h"
#include "mvlc_util.h"
#include "mvme_session.h"
#include "vmeconfig_to_crateconfig.h"

using namespace mesytec;

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    mvme_init("mvme_crateconfig_tool");
    QCoreApplication app(argc, argv);
    int verbosity = 0;

    auto args = app.arguments();

    if (!args.isEmpty()) args.pop_front();

    while (true)
    {
        if (auto idx = args.indexOf("--verbose"); idx >= 0)
        {
            ++verbosity;
            args.removeAt(idx);
        }
        else if (auto idx = args.indexOf("-v"); idx >= 0)
        {
            ++verbosity;
            args.removeAt(idx);
        }
        else
            break;
    }

    if (args.size() < 1)
    {
        std::cerr << "Error: no path to VMEConfig given" << std::endl;
        return 1;
    }

    std::string vmeConfigPath(args.at(0).toStdString());
    std::cout << fmt::format("Reading VMEConfig from {}", vmeConfigPath) << std::endl;

    auto [vmeConfig, errStr] = read_vme_config_from_file(QString::fromStdString(vmeConfigPath));

    if (!vmeConfig)
    {
        std::cerr << fmt::format("Error reading VMEConfig from {}: {}", vmeConfigPath, errStr.toStdString()) << std::endl;;
        return 1;
    }

    auto crateConfig = mvme::vmeconfig_to_crateconfig(vmeConfig.get());


    // Compare flattened readout stack commands
    // ========================================
    {
        auto crateConfStacks = crateConfig.stacks;
        auto vmeConfStacks = mvme_mvlc::get_readout_stacks(*vmeConfig);
        const auto maxStacks = std::max(crateConfStacks.size(), vmeConfStacks.size());

        for (size_t stackId = 0; stackId < maxStacks; ++stackId)
        {
            if (stackId < crateConfStacks.size() && stackId < vmeConfStacks.size())
            {
                // Note: using getCommands() on the stacks here to ignore grouping
                // differences between the stacks. The flat list of commands should
                // be identical between both configs.
                const auto &crateConfStack = crateConfStacks[stackId];
                const auto crateConfCmds = crateConfStack.getCommands();
                const auto &vmeConfStack = vmeConfStacks[stackId];
                const auto vmeConfCmds = vmeConfStack.getCommands();

                if (crateConfCmds != vmeConfCmds)
                {
                    std::cout << fmt::format("Stacks with stackId={} differ!\n", stackId);
                    std::cout << fmt::format("Stack{} from VMEConfig:\n{}\n", stackId, to_yaml(vmeConfStack));
                    std::cout << fmt::format("Stack{} from CrateConfig:\n{}\n", stackId, to_yaml(crateConfStack));
                }
                else
                    std::cout << fmt::format("Stacks with stackId={} are equal.\n", stackId);
            }
            else if (stackId < crateConfStacks.size())
            {
                const auto &crateConfStack = crateConfStacks[stackId];
                std::cout << fmt::format("Stack with stackId={} only present in CrateConfig!\n", stackId);
                std::cout << fmt::format("Stack{} from CrateConfig:\n{}", stackId, to_yaml(crateConfStack));
            }
            else if (stackId < vmeConfStacks.size())
            {
                const auto &vmeConfStack = vmeConfStacks[stackId];
                std::cout << fmt::format("Stack with stackId={} only present in VMEConfig!\n", stackId);
                std::cout << fmt::format("Stack{} from VMEConfig:\n{}", stackId, to_yaml(vmeConfStack));
            }
        }
    }

    // Compare stack trigger values
    // ============================
    {
        auto crateConfTriggers = crateConfig.triggers;
        auto [vmeConfTriggers, ec] = mvme_mvlc::get_trigger_values(*vmeConfig);

        if (ec)
        {
            std::cerr << fmt::format("Error getting trigger values from VMEConfig: {}", ec.message());
        }
        else if (crateConfTriggers != vmeConfTriggers)
        {
            std::cout << fmt::format("Trigger values differ!\n");
            std::cout << fmt::format("Trigger values from VMEConfig: {:02X}\n", fmt::join(vmeConfTriggers, ", "));
            std::cout << fmt::format("Trigger values from CrateConfig: {:02X}\n", fmt::join(crateConfTriggers, ", "));
        }
        else
        {
            std::cout << "Trigger values are equal.\n";
        }
    }

    // Compare mvlc trigger/io init sequences
    // ======================================
    {
        auto vmeConfTriggerIo = mvme_mvlc::update_trigger_io(*vmeConfig);
        auto vmeConfTriggerIoScriptText = mvme_mvlc::trigger_io::generate_trigger_io_script_text(vmeConfTriggerIo);
        auto vmeConfTriggerIoScript = vme_script::parse(vmeConfTriggerIoScriptText);
        auto vmeConfTriggerIoBuilder = mvme_mvlc::build_mvlc_stack(vmeConfTriggerIoScript);
        auto crateConfTriggerIoBuilder = crateConfig.initTriggerIO;

        // again compare the flat lists of commands, not the individual groups making up the stack
        if (vmeConfTriggerIoBuilder.getCommands() != crateConfTriggerIoBuilder.getCommands())
        {
            std::cout << "TriggerIO setups differ!\n";
            std::cout << fmt::format("TriggerIO from VMEConfig:\n{}\n", to_yaml(vmeConfTriggerIoBuilder));
            std::cout << fmt::format("TriggerIO from CrateConfig:\n{}\n", to_yaml(crateConfTriggerIoBuilder));
        }
        else
            std::cout << "TriggerIO setups are equal.\n";
    }

    // Compare module init commands
    // ============================

    // Compare daq stop commands
    // =========================

    // Compare mcstDaqStart
    // ====================

    // Compare mcstDaqStop
    // ====================

    mvme_shutdown();
    return 0;
}
