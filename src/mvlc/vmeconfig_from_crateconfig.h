#ifndef __MVME_VMECONFIG_FROM_CRATECONFIG_H__
#define __MVME_VMECONFIG_FROM_CRATECONFIG_H__

#include "vme_config.h"
#include <mesytec-mvlc/mvlc_readout_config.h>

namespace mesytec
{
namespace mvme
{

std::unique_ptr<VMEConfig> LIBMVME_EXPORT vmeconfig_from_crateconfig(
    const mvlc::CrateConfig &crateConfig);

}
}

#endif /* __MVME_VMECONFIG_FROM_CRATECONFIG_H__ */
