#ifndef __MVME_VMECONFIG_TO_CRATECONFIG_H__
#define __MVME_VMECONFIG_TO_CRATECONFIG_H__

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "libmvme_export.h"
#include "mesytec-mvlc/mvlc_command_builders.h"
#include "vme_config.h"
#include "vme_script.h"

namespace mesytec
{
namespace mvme
{

mvlc::StackCommand vme_script_command_to_mvlc_command(const vme_script::Command &srcCmd);

std::vector<mvlc::StackCommand> convert_script(const vme_script::VMEScript &script);

std::vector<mvlc::StackCommand> convert_script(const VMEScriptConfig *script, u32 vmeBaseAddress = 0);

// Converts a mvme VMEConfig to a mesytec-mvlc CrateConfig.
mvlc::CrateConfig LIBMVME_EXPORT vmeconfig_to_crateconfig(const VMEConfig *vmeConfig);

}
}

#endif /* __MVME_VMECONFIG_TO_CRATECONFIG_H__ */
