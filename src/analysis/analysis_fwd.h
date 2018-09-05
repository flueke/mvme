#ifndef __ANALYSIS_FWD_H__
#define __ANALYSIS_FWD_H__

#include <memory>
#include <QHash>
#include <QSet>
#include <QVector>

namespace analysis
{

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
using OperatorVector = QVector<OperatorPtr>;
using PipeSourcePtr = std::shared_ptr<PipeSourceInterface>;
using SinkPtr = std::shared_ptr<SinkInterface>;
using SinkVector = QVector<SinkPtr>;
using SourcePtr = std::shared_ptr<SourceInterface>;
using SourceVector = QVector<SourcePtr>;

struct ConditionLink;

using ConditionLinks = QHash<OperatorPtr, ConditionLink>;

} // end namespace analysis

#endif /* __ANALYSIS_FWD_H__ */
