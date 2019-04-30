#ifndef __MVME_MVLC_DAQ_H__
#define __MVME_MVLC_DAQ_H__

#include "mvlc/mvlc_vme_controller.h"
#include "vme_config.h"

namespace mesytec
{
namespace mvlc
{

using Logger = std::function<void (const QString &)>;

void setup_mvlc(MVLC_VMEController &mvlc, VMEConfig *vmeConfig, Logger logger);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_DAQ_H__ */
