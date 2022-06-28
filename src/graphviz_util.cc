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

}
}