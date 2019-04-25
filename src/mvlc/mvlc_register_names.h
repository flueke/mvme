#ifndef __MVME_MVLC_REGISTER_NAMES_H__
#define __MVME_MVLC_REGISTER_NAMES_H__

#include <map>
#include <string>

#ifdef QT_CORE_LIB
#include <QMap>
#include <QString>
#endif

#include "typedefs.h"

namespace mesytec
{
namespace mvlc
{

const std::map<u16, std::string> &get_addr_2_name_map();
const std::map<std::string, u16> &get_name_2_addr_map();

#ifdef QT_CORE_LIB
const QMap<u16, QString> &get_addr_2_name_qmap();
const QMap<QString, u16> &get_name_2_addr_qmap();
#endif

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVME_MVLC_REGISTER_NAMES_H__ */
