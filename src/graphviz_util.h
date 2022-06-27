#ifndef __MVME_GRAPHVIZ_UTIL_H__
#define __MVME_GRAPHVIZ_UTIL_H__

#include <QByteArray>
#include <QDomDocument>
#include <QGraphicsSvgItem>
#include <QString>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <set>
#include "libmvme_export.h"

// Approach:
// load DOT code -> use graphviz to render to svg -> use QXmlStreamReader and
// QSvgRenderer to load and render the svg data. Use the xml reader to get the
// ids of svg items that should be rendered.
// For each item create a QGraphicsSvgItem using the shared renderer.
//
// Problem: cannot change attributes like the background color of the svg
// graphics items. QGraphicsSvgItem does not expose any functionality to
// influcence the drawing (probably as the rendering is done by the svg renderer).
//
// Solution: use QDomDocument and manipulate DOM attributes on the fly to
// change the apperance of the rendered SVG.


namespace mesytec
{
namespace graphviz_util
{
    // Relevant documentation:
    // - https://graphviz.org/pdf/libguide.pdf
    // - https://graphviz.org/pdf/cgraph.pdf

    // Does the gvLayout and gvRenderData steps. Not optimized at all as it
    // creates and destroys a gvContext. Uses a global mutex to ensure the
    // function is not invoked concurrently. Uses a global error buffer to store
    // graphviz generated warnings and errors.
    LIBMVME_EXPORT std::string layout_and_render_dot(
        const char *dotCode,
        const char *layoutEngine = "dot",
        const char *outputFormat = "svg");

    LIBMVME_EXPORT std::string get_error_buffer();

    // Qt container versions of the above functions. Even slower as internally
    // the non-qt versions are used and the results converted.
    LIBMVME_EXPORT QByteArray layout_and_render_dot_q(
        const QString &dotCode,
        const char *layoutEngine = "dot",
        const char *outputFormat = "svg");

    LIBMVME_EXPORT QString get_error_buffer_q();

    // The following is not limited to graphviz but should work on SVGs in
    // general but it was developed together with the graphviz rendering code.

enum class DomVisitResult
{
    Continue,
    Stop
};

using DomNodeVisitor = std::function<DomVisitResult (const QDomNode &node, int depth)>;

// Depth first search starting at the given root node.
void visit_dom_nodes(const QDomNode &node, int depth, const DomNodeVisitor &f)
{
    if (f(node, depth) == DomVisitResult::Stop)
        return;

    auto n = node.firstChild();

    while (!n.isNull())
    {
        visit_dom_nodes(n, depth+1, f);
        n = n.nextSibling();
    }
}

inline void visit_dom_nodes(const QDomNode &root, const DomNodeVisitor &f)
{
    visit_dom_nodes(root, 0, f);
}

inline void visit_dom_nodes(QDomDocument &doc, const DomNodeVisitor &f)
{
    auto n = doc.documentElement();
    visit_dom_nodes(n, f);
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

    auto f = [&] (const QDomNode &n, int depth) -> DomVisitResult
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

    visit_dom_nodes(root, f);

    return result;
}

QDomElement find_element_by_id(const QDomNode &root, const QString &id)
{
    auto predicate = [&id] (const QDomElement &e, int)
    {
        return (e.hasAttribute("id") && e.attribute("id") == id);
    };

    return find_element_by_predicate(root, predicate);
}

QDomElement find_element_by_id(const QDomDocument &doc, const QString &id)
{
    return find_element_by_id(doc.documentElement(), id);
}

class DomAndRenderer
{
    public:
        DomAndRenderer(const QByteArray &svgData)
            : renderer_(std::make_shared<QSvgRenderer>(svgData))
        {
            dom_.setContent(svgData);
        }

        QSvgRenderer *renderer() { return renderer_.get(); }
        QDomDocument dom() const { return dom_; }
        void reload()
        {
            renderer_->load(dom_.toByteArray());
        }

    private:
        std::shared_ptr<QSvgRenderer> renderer_;
        QDomDocument dom_;
};

class DomElementSvgItem : public QGraphicsSvgItem
{
    public:
        DomElementSvgItem(const DomAndRenderer &dr,
                          const QString &elementId,
                          QGraphicsItem *parentItem = nullptr)
            : QGraphicsSvgItem(parentItem), dr_(dr)
        {
            setSharedRenderer(dr_.renderer());
            setElementId(elementId);
            auto bounds = dr_.renderer()->boundsOnElement(elementId);
            setPos(bounds.x(), bounds.y());
        }

        QDomElement getRootElement() const
        {
            return find_element_by_id(dr_.dom(), elementId());
        }

        QDomElement getSvgShapeElement() const
        {
            return find_first_basic_svg_shape_element(getRootElement());
        }

        void setFillColor(const QColor &c)
        {
            auto shapeElement = getSvgShapeElement();

            if (!shapeElement.isNull())
            {
                shapeElement.setAttribute("fill", c.name());
                dr_.reload();
            }
        }

    private:
        DomAndRenderer dr_;
};

std::vector<std::unique_ptr<DomElementSvgItem>> create_svg_graphics_items(
    const QByteArray &svgData,
    const DomAndRenderer &dr,
    const std::set<QString> acceptedElementClasses = { "node", "edge", "cluster" })
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

#endif /* __MVME_GRAPHVIZ_UTIL_H__ */
