#include "mvme_qthelp.h"
#include "git_sha1.h"

namespace mesytec
{
namespace mvme
{

QString get_mvme_qthelp_index_url()
{
    return (QStringLiteral("qthelp://com.mesytec.mvme.%1/doc/index.html")
            .arg(GIT_VERSION_SHORT));
}

}
}
