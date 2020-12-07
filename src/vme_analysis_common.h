/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef __VME_ANALYSIS_COMMON_H__
#define __VME_ANALYSIS_COMMON_H__

#include <QDebug>
#include <chrono>
#include <cmath>

#include "libmvme_export.h"
#include "vme_config.h"

namespace analysis
{
class Analysis;
}

namespace vme_analysis_common
{

using LoggerFun = std::function<void (const QString &)>;

/** Adds information about each module in the vmeConfig to the analysis.
 * The information is stored as a dynamic QObject property using the name "ModuleProperties".
 */
void add_vme_properties_to_analysis(const VMEConfig *vmeConfig, analysis::Analysis *analysis);

struct ModuleInfo
{
    QUuid id;
    QString typeName;
    QString name;
    QUuid eventId; // only set if the object was obtained from the VMEConfig
};

QDebug &operator<<(QDebug &dbg, const ModuleInfo &mi);

QVector<ModuleInfo> get_module_infos(const VMEConfig *vmeConfig);
QVector<ModuleInfo> get_module_infos(analysis::Analysis *analysis);

bool auto_assign_vme_modules(const VMEConfig *vmeConfig, analysis::Analysis *analysis, LoggerFun logger = LoggerFun());
bool auto_assign_vme_modules(QVector<ModuleInfo> vmeModuleInfos, analysis::Analysis *analysis, LoggerFun logger = LoggerFun());

bool run_vme_analysis_module_assignment_ui(const VMEConfig *vmeConfig, analysis::Analysis *analysis, QWidget *parent = 0);
bool run_vme_analysis_module_assignment_ui(QVector<ModuleInfo> vmeModuleInfos, analysis::Analysis *analysis, QWidget *parent = 0);

/** Removes sources and operators that are not attached to the module specified
 * by moduleId and eventId from the given analysis. */
void remove_analysis_objects_unless_matching(analysis::Analysis *analysis, const QUuid &moduleId, const QUuid &eventId);

/** Removes sources and operators that are not attached to the module specified
 * by moduleInfo from the given analysis. */
void remove_analysis_objects_unless_matching(analysis::Analysis *analysis, const ModuleInfo &moduleInfo);

/** Removes sources and operators from the analysis which reference modules and
 * events that do not exist in vmeConfig. */
void remove_analysis_objects_unless_matching(analysis::Analysis *analysis, const VMEConfig *vmeConfig);

struct VMEConfigIndex
{
    s32 eventIndex = -1;
    s32 moduleIndex = -1;

    inline bool isValid() const { return eventIndex >= 0 && moduleIndex >= 0; }
    inline bool isValidEvent() const { return eventIndex >= 0; }
};

inline bool operator==(const VMEConfigIndex &a, const VMEConfigIndex &b)
{
    return a.eventIndex == b.eventIndex
        && a.moduleIndex == b.moduleIndex;
}

using VMEIdToIndex = QHash<QUuid, VMEConfigIndex>;

LIBMVME_EXPORT VMEIdToIndex build_id_to_index_mapping(const VMEConfig *vmeConfig);

class TimetickGenerator
{
    public:
        using ClockType = std::chrono::steady_clock;

        TimetickGenerator()
            : t_start(ClockType::now())
        {}

        int generateElapsedSeconds()
        {
            int result = 0;
            auto t_end = ClockType::now();
            std::chrono::duration<double, std::milli> diff = t_end - t_start;
            double elapsed_ms = diff.count() + remainder_ms;

            if (elapsed_ms >= 1000.0)
            {
                result = std::floor(elapsed_ms / 1000.0);
                remainder_ms = std::fmod(elapsed_ms, 1000.0);
                t_start = t_end;
            }

            return result;
        }

        double getTimeToNextTick_ms() const
        {
            return 1000.0 - remainder_ms;
        }

    private:
        ClockType::time_point t_start;
        double remainder_ms = 0.0;

};

}

#endif /* __VME_ANALYSIS_COMMON_H__ */
