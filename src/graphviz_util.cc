#include "graphviz_util.h"

#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <mutex>
#include <sstream>
#include <QGraphicsScene>
#include <QDebug>

extern gvplugin_library_t gvplugin_dot_layout_LTX_library;
#if 0
extern gvplugin_library_t gvplugin_neato_layout_LTX_library;
extern gvplugin_library_t gvplugin_core_LTX_library;
extern gvplugin_library_t gvplugin_quartz_LTX_library;
extern gvplugin_library_t gvplugin_visio_LTX_library;
#endif

lt_symlist_t lt_preloaded_symbols[] =
{
    { "gvplugin_dot_layout_LTX_library", &gvplugin_dot_layout_LTX_library},
    #if 0
    { "gvplugin_neato_layout_LTX_library", &gvplugin_neato_layout_LTX_library},
    { "gvplugin_core_LTX_library", &gvplugin_core_LTX_library},
    { "gvplugin_quartz_LTX_library", &gvplugin_quartz_LTX_library},
    { "gvplugin_visio_LTX_library", &gvplugin_visio_LTX_library},
    #endif
    { 0, 0}
};

namespace mesytec
{
namespace graphviz_util
{

static std::mutex g_mutex;
static std::stringstream g_errorBuffer;

extern gvplugin_library_t gvplugin_dot_layout_LTX_library;

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
    agseterrf(myerrf);

    GVC_t *gvc = gvContext();
    char *args[] = { {"mvme" }, {"-v"} };
    gvParseArgs(gvc, 2, args);

    gvAddLibrary(gvc, &gvplugin_dot_layout_LTX_library);
    qDebug() << "gv error buffer after gvAddLibrary():" << g_errorBuffer.str().c_str();

    Agraph_t *g = agmemread(dotCode);
    qDebug() << "gv error buffer after agmemread():" << g_errorBuffer.str().c_str();

    if (!g)
        return {};

    gvLayout(gvc, g, layoutEngine);
    qDebug() << "gv error buffer after gvLayout():" << g_errorBuffer.str().c_str();

    char *renderDest = nullptr;
    unsigned int renderSize = 0;
    gvRenderData(gvc, g, outputFormat, &renderDest, &renderSize);
    qDebug() << "gv error buffer after gvRenderData():" << g_errorBuffer.str().c_str();

    qDebug() << "layout_and_render_dot: errorbuffer after gvRenderData" << g_errorBuffer.str().c_str();

    std::string svgData{renderDest ? renderDest : ""};

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

QByteArray layout_and_render_dot_q(
    const std::string &dotCode,
    const char *layoutEngine,
    const char *outputFormat)
{
    return QByteArray(
        layout_and_render_dot(
            dotCode.c_str(),
            layoutEngine, outputFormat)
            .c_str());
}

QString get_error_buffer_q()
{
    return QString::fromStdString(get_error_buffer());
}

QDomElement find_first_basic_svg_shape_element(const QDomNode &root)
{
    static const std::set<QString> SvgBasicShapes = {
        "circle", "ellipse", "line", "polygon", "polyline", "rect"
    };

    QDomElement result;

    auto f = [&result] (const QDomNode &node, int) -> DomVisitResult
    {
        if (!result.isNull())
            return DomVisitResult::Stop;

        auto e = node.toElement();

        if (!e.isNull() && SvgBasicShapes.count(e.tagName()))
        {
            result = e;
            return DomVisitResult::Stop;
        }

        return DomVisitResult::Continue;
    };

    visit_dom_nodes(root, f);

    return result;
}

QDomElement find_element_by_predicate(
    const QDomNode &root,
    const std::function<bool (const QDomElement &e, int depth)> &predicate)
{
    QDomElement result;

    auto visitor = [&] (const QDomNode &n, int depth) -> DomVisitResult
    {
        if (!result.isNull())
           return DomVisitResult::Stop;

        auto e = n.toElement();

        if (!e.isNull() && predicate(e, depth))
        {
            result = e;
            return DomVisitResult::Stop;
        }

        return DomVisitResult::Continue;
    };

    visit_dom_nodes(root, visitor);

    return result;
}

std::vector<std::unique_ptr<DomElementSvgItem>> create_svg_graphics_items(
    const QByteArray &svgData,
    const DomAndRenderer &dr,
    const std::set<QString> &acceptedElementClasses)
{
    std::vector<std::unique_ptr<DomElementSvgItem>> result;

    QXmlStreamReader xmlReader(svgData);
    while (!xmlReader.atEnd())
    {
        switch (xmlReader.readNext())
        {
            case QXmlStreamReader::TokenType::StartElement:
            {
                auto attributes = xmlReader.attributes();
                auto elementClass = attributes.value("class").toString();

                if (!acceptedElementClasses.count(elementClass))
                    continue;

                if (attributes.hasAttribute("id"))
                {
                    auto elementId = attributes.value("id").toString();
                    auto svgItem = std::make_unique<DomElementSvgItem>(dr, elementId);
                    result.emplace_back(std::move(svgItem));
                }
            } break;

            default:
                break;
        }
    }

    return result;
}

void DotGraphicsSceneManager::setDot(const std::string &dotStr)
{
    m_scene->clear();

    m_dotErrorBuffer = {};
    m_dotStr = dotStr;
    m_svgData = mesytec::graphviz_util::layout_and_render_dot_q(m_dotStr);
    m_dotErrorBuffer = get_error_buffer();
    m_dr = { m_svgData };
    auto items = mesytec::graphviz_util::create_svg_graphics_items(m_svgData, m_dr);

    for (auto &item: items)
        m_scene->addItem(item.release());

    // For the scene to recalculate the scene rect based on the items present.
    // This is the only way to actually shrink the scene rect.
    m_scene->setSceneRect(m_scene->itemsBoundingRect());
}

}
}
