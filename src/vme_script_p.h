#ifndef __MVME_VME_SCRIPT_P_H__
#define __MVME_VME_SCRIPT_P_H__

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vme_script
{

// Internal vme_script parser support functions.
// Kept in a separate header to make them available for automatic tests.

std::pair<std::string, bool> read_atomic_variable_reference(std::istringstream &in);
std::pair<std::string, bool> read_atomic_variable_reference(const std::string &str);

std::pair<std::string, bool> read_atomic_expression(std::istringstream &in);
std::pair<std::string, bool> read_atomic_expression(const std::string &str);

std::vector<std::string> split_into_atomic_parts(const std::string &line, int lineNumber);

} // end namespace vme_script

#endif /* __MVME_VME_SCRIPT_P_H__ */
