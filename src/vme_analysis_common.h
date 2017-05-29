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

struct ModuleInfo
{
    QUuid id;
    QString typeName;
    QString name;
    QUuid eventId; // only set if the object was obtained from the VMEConfig
};

QVector<ModuleInfo> get_module_infos(VMEConfig *vmeConfig);
QVector<ModuleInfo> get_module_infos(analysis::Analysis *analysis);

bool auto_assign_vme_modules(VMEConfig *vmeConfig, analysis::Analysis *analysis);
bool auto_assign_vme_modules(QVector<ModuleInfo> vmeModuleInfos, analysis::Analysis *analysis);

bool run_vme_analysis_module_assignment_ui(VMEConfig *vmeConfig, analysis::Analysis *analysis, QWidget *parent = 0);
bool run_vme_analysis_module_assignment_ui(QVector<ModuleInfo> vmeModuleInfos, analysis::Analysis *analysis, QWidget *parent = 0);

void remove_analysis_objects_unless_matching(analysis::Analysis *analysis, const ModuleInfo &moduleInfo);

}

#endif /* __VME_ANALYSIS_COMMON_H__ */
