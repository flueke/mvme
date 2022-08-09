#ifndef __MVME_GRAPHVIZ_UTIL_H__
#define __MVME_GRAPHVIZ_UTIL_H__

#include <QByteArray>
#include <QDomDocument>
#include <QGraphicsScene>
#include <QGraphicsSvgItem>
#include <QGraphicsView>
#include <QString>
#include <QSvgRenderer>
#include <QWidget>
#include <regex>
#include <set>
#include <string>
#include <utility>

#include "libmvme_export.h"

// Graphviz utilities for generating, layouting and rendering DOT code.
//
// The approach taken is to generate DOT code, render it to SVG using graphviz,
// then walk the generated SVG XML code to create QGraphicsSvgItems for desired
// elements.
//
// One problem of this approach is that QGraphicsSvgItems do not have methods
// to set e.g. the foreground and background colors/brushes. Solution: use
// QDomDocument to manipulate the SVG DOM attributes on the fly which does
// change the apperance of the QGraphicsSvgItems.

namespace mesytec
{
namespace graphviz_util
{

// Relevant documentation:
// - https://graphviz.org/pdf/libguide.pdf
// - https://graphviz.org/pdf/cgraph.pdf

// Does the gvLayout and gvRenderData steps. Not optimized at all as it creates
// and destroys a gvContext. Uses a global mutex to ensure graphviz code is not
// called concurrently. Uses a global error buffer to store graphviz generated
// warnings and errors.
LIBMVME_EXPORT std::string layout_and_render_dot(
    const char *dotCode,
    const char *layoutEngine = "dot",
    const char *outputFormat = "svg");

// Qt container versions of the above functions. Even slower as internally the
// non-qt data types are used and the results converted.
LIBMVME_EXPORT QByteArray layout_and_render_dot_q(
    const QString &dotCode,
    const char *layoutEngine = "dot",
    const char *outputFormat = "svg");

LIBMVME_EXPORT QByteArray layout_and_render_dot_q(
    const std::string &dotCode,
    const char *layoutEngine = "dot",
    const char *outputFormat = "svg");

// Returns the contents of the graphviz error message buffer.
LIBMVME_EXPORT std::string get_error_buffer();

// Clears the graphviz error buffer.
LIBMVME_EXPORT void clear_error_buffer();

LIBMVME_EXPORT QString get_error_buffer_q();

// The following DOM related code is not limited to graphviz but should work on SVGs in general.

enum class DomVisitResult
{
    Continue,
    Stop
};

using DomNodeVisitor = std::function<DomVisitResult (const QDomNode &node)>;

// Depth first search starting at the given root node.
inline void visit_dom_nodes(const QDomNode &node, const DomNodeVisitor &f)
{
    if (f(node) == DomVisitResult::Stop)
        return;

    auto n = node.firstChild();

    while (!n.isNull())
    {
        visit_dom_nodes(n, f);
        n = n.nextSibling();
    }
}

inline void visit_dom_nodes(const QDomDocument &doc, const DomNodeVisitor &f)
{
    visit_dom_nodes(doc.documentElement(), f);
}

LIBMVME_EXPORT QDomElement find_first_basic_svg_shape_element(const QDomNode &root);

LIBMVME_EXPORT QDomElement find_element_by_predicate(
    const QDomNode &root,
    const std::function<bool (const QDomElement &e)> &predicate);

inline QDomElement find_element_by_id(const QDomNode &root, const QString &id)
{
    auto predicate = [&id] (const QDomElement &e)
    {
        return (e.hasAttribute("id") && e.attribute("id") == id);
    };

    return find_element_by_predicate(root, predicate);
}

inline QDomElement find_element_by_id(const QDomDocument &doc, const QString &id)
{
    return find_element_by_id(doc.documentElement(), id);
}

// Combines a QDomDocument and a QSvgRenderer, both using the same underlying
// svg data. After modifying the DOM contents use reload() to keep the
// SvgRenderer in sync.
class LIBMVME_EXPORT DomAndRenderer
{
    public:
        DomAndRenderer()
            : renderer_(std::make_shared<QSvgRenderer>())
        {
        }

        DomAndRenderer(const QByteArray &svgData)
            : renderer_(std::make_shared<QSvgRenderer>(svgData))
        {
            dom_.setContent(svgData);
        }

        QSvgRenderer *renderer() const { return renderer_.get(); };
        const QDomDocument &dom() const { return dom_; }

        void reloadFromDom()
        {
            renderer_->load(dom_.toByteArray());
        }

        void setDomContent(const QByteArray &svgData)
        {
            dom_.setContent(svgData);
            renderer_->load(svgData);
        }

        QByteArray domConent() const
        {
            return dom_.toByteArray();
        }

    private:
        QDomDocument dom_;
        // deliberately a shared_ptr to make the whole thing copyable
        std::shared_ptr<QSvgRenderer> renderer_;
};

class LIBMVME_EXPORT DomElementSvgItem : public QGraphicsSvgItem
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
                dr_.reloadFromDom();
            }
        }

    private:
        DomAndRenderer dr_;
};

class LIBMVME_EXPORT EdgeItem: public QGraphicsItem
{
    public:

};

class LIBMVME_EXPORT SvgItemFactory
{
    public:
        virtual ~SvgItemFactory() {}
        virtual std::unique_ptr<QGraphicsItem> operator()(const QDomElement &e) const = 0;
};

class DefaultSvgItemFactory: public SvgItemFactory
{
    public:
        DefaultSvgItemFactory(const DomAndRenderer &dr)
           : dr_(dr)
        { }

        std::unique_ptr<QGraphicsItem> operator()(const QDomElement &e) const override;

    private:
       DomAndRenderer dr_;
};

std::vector<std::unique_ptr<QGraphicsItem>> create_svg_graphics_items(
    const DomAndRenderer &dr,
    const SvgItemFactory &itemFactory);

class LIBMVME_EXPORT DotSvgGraphicsSceneManager
{
    public:
        DotSvgGraphicsSceneManager()
            : m_scene(std::make_unique<QGraphicsScene>())
        {
        }

        DotSvgGraphicsSceneManager(std::unique_ptr<QGraphicsScene> &&scene)
            : m_scene(std::move(scene))
        {
        }

        QGraphicsScene *scene() const { return m_scene.get(); }
        std::string dotString() const { return m_dotStr; }
        QByteArray svgData() const { return m_svgData; }
        QDomDocument dom() const { return m_dr.dom(); }
        QSvgRenderer *renderer() const { return m_dr.renderer(); }
        std::string dotErrorBuffer() const { return m_dotErrorBuffer; }

        void setDot(const QString &dotStr) { setDot(dotStr.toStdString()); }
        void setDot(const std::string &dotStr);

    private:
        std::unique_ptr<QGraphicsScene> m_scene;
        std::string m_dotStr;
        QByteArray m_svgData;
        DomAndRenderer m_dr;
        std::string m_dotErrorBuffer;
};

class LIBMVME_EXPORT DotSvgWidget: public QWidget
{
    Q_OBJECT
    public:
        explicit DotSvgWidget(QWidget *parent = nullptr);
        virtual ~DotSvgWidget() override;

    void setDot(const std::string &dotStr);
    DotSvgGraphicsSceneManager *sceneManager() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

inline std::string escape_dot_string(std::string label)
{
    label = std::regex_replace(label, std::regex("&"), "&amp;");
    label = std::regex_replace(label, std::regex("\""), "&quot;");
    label = std::regex_replace(label, std::regex(">"), "&gt;");
    label = std::regex_replace(label, std::regex("<"), "&lt;");
    return label;
}

inline std::string escape_dot_string(const QString &label)
{
    return escape_dot_string(label.toStdString());
}

inline QString escape_dot_string_q(const QString &label)
{
    return QString::fromStdString(escape_dot_string(label));
}

// Creates a QGraphicsView initialized for rendering graphviz objects.
LIBMVME_EXPORT QGraphicsView *make_graph_view();

}
}

#endif /* __MVME_GRAPHVIZ_UTIL_H__ */
