#ifndef __ANALYSIS_SESSION_H__
#define __ANALYSIS_SESSION_H__

#include <QPair>
#include <QString>

namespace analysis
{
class Analysis;
}

QPair<bool, QString> save_analysis_session(const QString &filename, analysis::Analysis *analysis);

#endif /* __ANALYSIS_SESSION_H__ */
