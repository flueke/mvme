#ifndef __MVME_DATA_EXPORT_UTIL_H__
#define __MVME_DATA_EXPORT_UTIL_H__

#include "analysis/analysis.h"
#include "vme_config.h"
#include "event_server/common/event_server_lib.h"

struct OutputDataDescription
{
    mvme::event_server::EventDataDescriptions eventDataDescriptions;
    mvme::event_server::VMETree vmeTree;
};

OutputDataDescription make_output_data_description(const VMEConfig *vmeConfig,
                                                   const analysis::Analysis *analysis);

mvme::event_server::VMETree
make_vme_tree_description(const VMEConfig *vmeConfig);

mvme::event_server::EventDataDescriptions
make_datasource_descriptions(const analysis::Analysis *analysis);

#endif /* __MVME_DATA_EXPORT_UTIL_H__ */
