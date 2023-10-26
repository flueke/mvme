#ifndef __MVME_MVLC_READOUT_PARSER_SUPPORT_H__
#define __MVME_MVLC_READOUT_PARSER_SUPPORT_H__

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <vector>

#include "libmvme_mvlc_export.h"

#include "vme_script.h"

namespace mesytec
{
namespace mvme_mvlc
{

using ModuleReadoutParts = mvlc::readout_parser::ModuleReadoutStructure;

// VME module readout scripts indexed by event and module
using VMEConfReadoutScripts = std::vector<std::vector<vme_script::VMEScript>>;

#if 0

// ModuleReadoutParts indexed by event and module
using VMEConfReadoutInfo    = std::vector<std::vector<ModuleReadoutParts>>;

LIBMVME_EXPORT ModuleReadoutParts parse_module_readout_script(
    const vme_script::VMEScript &readoutScript);

LIBMVME_EXPORT VMEConfReadoutInfo parse_vme_readout_info(
    const VMEConfReadoutScripts &rdoScripts);
#endif

} // end namespace mvme_mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_READOUT_PARSER_SUPPORT_H__ */
