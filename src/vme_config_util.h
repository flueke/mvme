#ifndef __MVME_VME_CONFIG_UTIL_H__
#define __MVME_VME_CONFIG_UTIL_H__

#include <memory>

#include "libmvme_export.h"
#include "vme_config.h"

// Factory funcion to setup a new EventConfig instance which is to be added to
// the given VMEConfig.
// - A new unique objectName is set on the returned value.
// - The trigger IRQ value is set to the first unused irq in the system or '0' if
//   all irqs are in use.
// - The set of default variables for the event is created: sys_irq, mesy_mcst,
//   mesy_readout_num_events, mesy_eoe_marker.

std::unique_ptr<EventConfig> LIBMVME_EXPORT make_new_event_config(const VMEConfig *parentVMEConfig);

// Returns the first unused irq number in the given config or 0 if there are no
// unused irqs.
u8 LIBMVME_EXPORT get_next_free_irq(const VMEConfig *vmeConfig);

#endif /* __MVME_VME_CONFIG_UTIL_H__ */
