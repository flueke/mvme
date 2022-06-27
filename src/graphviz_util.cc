#include "graphviz_util.h"

#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <mutex>
#include <sstream>

namespace mesytec
{
namespace graphviz_util
{

static std::mutex g_mutex;
static std::stringstream g_errorBuffer;

std::string layout_and_render_dot(
    const char *dotCode,
    const char *layoutEngine,
    const char *outputFormat)
{
    auto myerrf = [] (char *msg) -> int
    {
        g_errorBuffer << msg;
        return 0;
    };

    std::lock_guard<std::mutex> guard(g_mutex);

    g_errorBuffer.str({}); // clear the error buffer

    GVC_t *gvc = gvContext();

    agseterrf(myerrf);

    Agraph_t *g = agmemread(dotCode);

    gvLayout(gvc, g, layoutEngine);

    char *renderDest = nullptr;
    unsigned int renderSize = 0;
    gvRenderData(gvc, g, outputFormat, &renderDest, &renderSize);

    std::string svgData(renderDest);

    gvFreeRenderData(renderDest);
    gvFreeLayout(gvc, g);
    agclose(g);
    gvFreeContext(gvc);

    return svgData;
}

std::string get_error_buffer()
{
    std::lock_guard<std::mutex> guard(g_mutex);
    return g_errorBuffer.str();
}

QByteArray layout_and_render_dot_q(
    const QString &dotCode,
    const char *layoutEngine,
    const char *outputFormat)
{
    return QByteArray(
        layout_and_render_dot(
            dotCode.toLocal8Bit().data(),
            layoutEngine, outputFormat)
            .c_str());
}

QString get_error_buffer_q()
{
    return QString::fromStdString(get_error_buffer());
}

}
}