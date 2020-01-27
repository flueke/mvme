#ifndef __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__
#define __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__

#include <QJsonObject>

namespace mvme
{
namespace vme_config_json
{

// Conversion from older VMEConfig JSON formats to the latest version.
// The given JSON must be the root of a VMEConfig object.
QJsonObject convert_vmeconfig_to_current_version(QJsonObject json);

int get_vmeconfig_version(const QJsonObject &json);

} // end namespace vme_config_json
} // end namespace mvme

#endif /* __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__ */
