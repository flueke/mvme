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
#ifndef __ANALYSIS_FWD_H__
#define __ANALYSIS_FWD_H__

#include <memory>
#include <QHash>
#include <QSet>
#include <QVector>

namespace analysis
{

struct Slot;
class Pipe;
class AnalysisObject;
class ConditionInterface;
class Directory;
class OperatorInterface;
class PipeSourceInterface;
class SinkInterface;
class SourceInterface;

class Analysis;

using AnalysisObjectPtr = std::shared_ptr<AnalysisObject>;
using AnalysisObjectSet = QSet<AnalysisObjectPtr>;
using AnalysisObjectVector = QVector<AnalysisObjectPtr>;
using ConditionPtr = std::shared_ptr<ConditionInterface>;
using ConditionVector = QVector<ConditionPtr>;
using DirectoryPtr = std::shared_ptr<Directory>;
using DirectoryVector = QVector<DirectoryPtr>;
using OperatorPtr = std::shared_ptr<OperatorInterface>;
using OperatorVector = std::vector<OperatorPtr>;
using PipeSourcePtr = std::shared_ptr<PipeSourceInterface>;
using SinkPtr = std::shared_ptr<SinkInterface>;
using SinkVector = QVector<SinkPtr>;
using SourcePtr = std::shared_ptr<SourceInterface>;
using SourceVector = std::vector<SourcePtr>;

using ConditionLinks = QHash<OperatorPtr, ConditionPtr>;

} // end namespace analysis

#endif /* __ANALYSIS_FWD_H__ */
