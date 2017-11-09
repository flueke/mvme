#ifndef __ANALYSIS_SESSION_H__
#define __ANALYSIS_SESSION_H__

#include <QPair>
#include <QString>
#include <QJsonDocument>

namespace analysis
{

class Analysis;

QPair<bool, QString> analysis_session_system_init();
QPair<bool, QString> analysis_session_system_destroy();

QPair<bool, QString> save_analysis_session(const QString &filename, analysis::Analysis *analysis);
QPair<bool, QString> load_analysis_session(const QString &filename, analysis::Analysis *analysis);
QPair<QJsonDocument, QString> load_analysis_config_from_session_file(const QString &filename);

} // namespace analysis

#endif /* __ANALYSIS_SESSION_H__ */
