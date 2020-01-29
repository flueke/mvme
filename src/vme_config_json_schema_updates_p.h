#ifndef __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_P_H__
#define __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_P_H__

#include <QString>
#include "typedefs.h"

namespace mvme
{
namespace vme_config
{
namespace json_schema
{

// Guess the high-byte of an events mcst.
// The input script must be one of the event daq start/stop scripts containing
// a write to the 'start/stop acq register' (0x603a).
u8 guess_event_mcst(const QString &eventScript);

} // end namespace json_schema
} // end namespace vme_config
} // end namespace mvme

#endif /* __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_P_H__ */
