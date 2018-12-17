#ifndef __MVME_DATA_EXPORT_UTIL_H__
#define __MVME_DATA_EXPORT_UTIL_H__

#include <QJsonObject>
#include <QJsonArray>

#include "analysis/analysis.h"
#include "vme_config.h"

QJsonObject make_output_data_description(const VMEConfig *vmeConfig,
                                         const analysis::Analysis *analysis);

QJsonValue make_vme_tree_description(const VMEConfig *vmeConfig);
QJsonValue make_datasource_description(const analysis::Analysis *analysis);

#endif /* __MVME_DATA_EXPORT_UTIL_H__ */
