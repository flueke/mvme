#include "mvlc/readout_parser_support.h"
#include "vme_script_util.h"

namespace mesytec
{
namespace mvme_mvlc
{

ModuleReadoutParts parse_module_readout_script(const vme_script::VMEScript &readoutScript)
{
    using namespace vme_script;
    enum State { Initial, Fixed, Dynamic };
    State state = Initial;
    ModuleReadoutParts modParts = {};

    for (auto &cmd: readoutScript)
    {
        if (cmd.type == CommandType::Read
            || cmd.type == CommandType::Marker)
        {
            switch (state)
            {
                case Initial:
                    state = Fixed;
                    ++modParts.len;
                    break;
                case Fixed:
                    ++modParts.len;
                    break;
                case Dynamic:
                    throw std::runtime_error("block read after fixed reads in module reaodut");
            }
        }
        else if (is_block_read_command(cmd.type))
        {
            switch (state)
            {
                case Initial:
                    state = Dynamic;
                    assert(modParts.len == 0);
                    modParts.len = -1;
                    break;
                case Fixed:
                    throw std::runtime_error("fixed read after block read in module readout");
                case Dynamic:
                    throw std::runtime_error("multiple block reads in module readout");
            }
        }
        // FIXME: handle mvlc_custom commands
    }

    return modParts;
}

VMEConfReadoutInfo parse_vme_readout_info(const VMEConfReadoutScripts &rdoScripts)
{
    VMEConfReadoutInfo result;

    for (const auto &eventScripts: rdoScripts)
    {
        std::vector<ModuleReadoutParts> moduleReadouts;

        for (const auto &moduleScript: eventScripts)
        {
            moduleReadouts.emplace_back(parse_module_readout_script(moduleScript));
        }

        result.emplace_back(moduleReadouts);
    }

    return result;
}

} // end namespace mvme_mvlc
} // end namespace mesytec
