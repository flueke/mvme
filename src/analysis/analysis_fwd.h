#ifndef __ANALYSIS_FWD_H__
#define __ANALYSIS_FWD_H__

#include <memory>
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
using AnalysisObjectVector = QVector<AnalysisObjectPtr>;
using AnalysisObjectSet = QSet<AnalysisObjectPtr>;
using PipeSourcePtr = std::shared_ptr<PipeSourceInterface>;
using SourcePtr = std::shared_ptr<SourceInterface>;
using SourceVector = QVector<SourcePtr>;
using OperatorPtr = std::shared_ptr<OperatorInterface>;
using OperatorVector = QVector<OperatorPtr>;
using SinkPtr = std::shared_ptr<SinkInterface>;
using SinkVector = QVector<SinkPtr>;
using DirectoryPtr = std::shared_ptr<Directory>;
using DirectoryVector = QVector<DirectoryPtr>;

} // end namespace analysis

#endif /* __ANALYSIS_FWD_H__ */
