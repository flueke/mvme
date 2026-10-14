#ifndef D238BFDE_B6CC_4602_8ADB_A96714E81B33
#define D238BFDE_B6CC_4602_8ADB_A96714E81B33

#include <string>
#include <vector>

#include "libmvme_export.h"

namespace mesytec::mvme::util
{

LIBMVME_EXPORT std::vector<unsigned> parse_version(const std::string &s);
LIBMVME_EXPORT bool version_less_than(const std::string &a, const std::string &b);

}

#endif /* D238BFDE_B6CC_4602_8ADB_A96714E81B33 */
