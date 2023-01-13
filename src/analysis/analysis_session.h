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
