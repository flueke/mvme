#include "expand_env_vars.h"
#include <QByteArray>
#include <QtGlobal>
#include <regex>

namespace mesytec::mvme::util
{

void expand_env_vars(std::string &text)
{
    static std::regex env("\\$\\{([^}]+)\\}");
    std::smatch match;
    while (std::regex_search(text, match, env))
    {
        // Could use the classic getenv() on unix instead of the qgetenv() wrapper.
        //const char *s = getenv(match[1].str().c_str());
        auto qbytes = qgetenv(match[1].str().c_str());
        const char *s = qbytes.data();
        const std::string var(s == NULL ? "" : s);
        text.replace(match[0].first, match[0].second, var);
    }
}

std::string expand_env_vars(const std::string &input)
{
    std::string text = input;
    expand_env_vars(text);
    return text;
}

} // namespace mesytec::mvme::util
