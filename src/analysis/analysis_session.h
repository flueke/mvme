#ifndef __MVME_ANALYSIS_SESSION_H__
#define __MVME_ANALYSIS_SESSION_H__

#include "libmvme_export.h"
#include <QPair>
#include <QString>
#include <QJsonDocument>
#include "qt_util.h"

namespace analysis
{

static const QString SessionFileFilter = QSL("MVME Analysis Sessions (*.msess);; All Files (*.*)");
static const QString SessionFileExtension = QSL(".msess");

class Analysis;

// save/load functions taking a filename argument
QPair<bool, QString> LIBMVME_EXPORT save_analysis_session(
    const QString &filename, analysis::Analysis *analysis);

QPair<bool, QString> LIBMVME_EXPORT load_analysis_session(
    const QString &filename, analysis::Analysis *analysis);

QPair<QJsonDocument, QString> LIBMVME_EXPORT load_analysis_config_from_session_file(
    const QString &filename);

// save/load functions working on a QIODevice
QPair<bool, QString> LIBMVME_EXPORT save_analysis_session_io(
    QIODevice &outdev, analysis::Analysis *analysis);

QPair<bool, QString> LIBMVME_EXPORT load_analysis_session_io(
    QIODevice &indev, analysis::Analysis *analysis);

QPair<QJsonDocument, QString> LIBMVME_EXPORT load_analysis_config_from_session_file_io(
    QIODevice &indev);

} // namespace analysis

#endif /* __MVME_ANALYSIS_SESSION_H__ */
