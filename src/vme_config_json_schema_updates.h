#ifndef __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__
#define __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__

#include <QJsonObject>

namespace mvme
{
namespace vme_config
{
namespace json_schema
{

void set_vmeconfig_version(QJsonObject &json, int version);
int get_vmeconfig_version(const QJsonObject &json);

// Conversion from older VMEConfig JSON formats to the latest version.
// The given JSON must be the root of a VMEConfig object.
QJsonObject convert_vmeconfig_to_current_version(QJsonObject json);

} // end namespace json_schema
} // end namespace vme_config
} // end namespace mvme

#endif /* __MVME_VME_CONFIG_JSON_CONFIG_CONVERSIONS_H__ */
