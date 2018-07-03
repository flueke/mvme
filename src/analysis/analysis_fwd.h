#ifndef __ANALYSIS_FWD_H__
#define __ANALYSIS_FWD_H__

#include <memory>
#include <QSet>
#include <QVector>

namespace analysis
{

class AnalysisObject;
class SourceInterface;
class OperatorInterface;
class SinkInterface;
class Directory;

class Analysis;

using AnalysisObjectPtr = std::shared_ptr<AnalysisObject>;
using AnalysisObjectVector = QVector<AnalysisObjectPtr>;
using AnalysisObjectSet = QSet<AnalysisObjectPtr>;

} // end namespace analysis

#endif /* __ANALYSIS_FWD_H__ */
