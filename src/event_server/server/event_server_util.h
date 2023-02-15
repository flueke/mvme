/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2023 mesytec GmbH & Co. KG <info@mesytec.com>
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
#ifndef __MVME_DATA_EXPORT_UTIL_H__
#define __MVME_DATA_EXPORT_UTIL_H__

#include "event_server/common/event_server_lib.h"

#include "analysis/analysis.h"
#include "vme_config.h"

struct OutputDataDescription
{
    mvme::event_server::EventDataDescriptions eventDataDescriptions;
    mvme::event_server::VMETree vmeTree;
};

OutputDataDescription
make_output_data_description(const VMEConfig *vmeConfig,
                             const analysis::Analysis *analysis);

mvme::event_server::VMETree
    make_vme_tree_description(const VMEConfig *vmeConfig);

mvme::event_server::EventDataDescriptions
    make_datasource_descriptions(const analysis::Analysis *analysis);

#endif /* __MVME_DATA_EXPORT_UTIL_H__ */
