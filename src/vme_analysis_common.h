#ifndef __VME_ANALYSIS_COMMON_H__
#define __VME_ANALYSIS_COMMON_H__

#include "vme_config.h"
#include "analysis/analysis.h"

namespace vme_analysis_common
{

/** Adds information about each module in the vmeConfig to the analysis.
 * The information is stored as a dynamic QObject property using the name "ModuleProperties".
 */
void add_vme_properties_to_analysis(VMEConfig *vmeConfig, analysis::Analysis *analysis);

bool auto_assign_vme_modules(VMEConfig *vmeConfig, analysis::Analysis *analysis);
bool run_vme_analysis_module_assignment_ui(VMEConfig *vmeConfig, analysis::Analysis *analysis, QWidget *parent = 0);

}

#endif /* __VME_ANALYSIS_COMMON_H__ */
