#ifndef __MVME_VMECONFIG_TO_CRATECONFIG_H__
#define __MVME_VMECONFIG_TO_CRATECONFIG_H__

#include <mesytec-mvlc/mesytec-mvlc.h>
#include "libmvme_export.h"
#include "vme_config.h"

namespace mesytec
{
namespace mvme
{

// Converts a mvme VMEConfig to a mesytec-mvlc CrateConfig.
mvlc::CrateConfig LIBMVME_EXPORT vmeconfig_to_crateconfig(const VMEConfig *vmeConfig);

}
}

#endif /* __MVME_VMECONFIG_TO_CRATECONFIG_H__ */
