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
#ifndef __MVME_VME_SCRIPT_P_H__
#define __MVME_VME_SCRIPT_P_H__

#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "libmvme_export.h"

namespace vme_script
{

// Internal vme_script parser support functions.
// Kept in a separate header to make them available for automatic tests.

// Run a pre parse step on the input.
// This splits the input into lines, removing comments and leading and trailing
// whitespace. The line is then further split into atomic parts and the
// variable names referenced whithin the line are collected.
QVector<PreparsedLine> LIBMVME_EXPORT pre_parse(const QString &input);
QVector<PreparsedLine> LIBMVME_EXPORT pre_parse(QTextStream &input);

std::pair<std::string, bool> LIBMVME_EXPORT read_atomic_variable_reference(std::istringstream &in);
std::pair<std::string, bool> LIBMVME_EXPORT read_atomic_variable_reference(const std::string &str);

std::pair<std::string, bool> LIBMVME_EXPORT read_atomic_expression(std::istringstream &in);
std::pair<std::string, bool> LIBMVME_EXPORT read_atomic_expression(const std::string &str);

std::vector<std::string> LIBMVME_EXPORT split_into_atomic_parts(const std::string &line, int lineNumber);

} // end namespace vme_script

#endif /* __MVME_VME_SCRIPT_P_H__ */
