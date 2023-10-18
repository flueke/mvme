#ifndef D238BFDE_B6CC_4602_8ADB_A96714E81B33
#define D238BFDE_B6CC_4602_8ADB_A96714E81B33

#include <string>
#include <vector>

namespace mesytec::mvme::util
{

std::vector<unsigned> parse_version(const std::string &s);
bool version_less_than(const std::string &a, const std::string &b);

}

#endif /* D238BFDE_B6CC_4602_8ADB_A96714E81B33 */
