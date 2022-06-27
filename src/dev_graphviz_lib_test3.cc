#include <cassert>
#include <fstream>
#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <QApplication>
#include <QDebug>
#include <QDomDocument>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSvgItem>
#include <QGraphicsView>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <sstream>
#include <set>
#include <mesytec-mvlc/util/logging.h>
#include "graphviz_util.h"

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

static const std::set<QString> SvgBasicShapes = { "circle", "ellipse", "line", "polygon", "polyline", "rect" };

QDomElement find_first_basic_svg_shape_element(const QDomNode &root)
{
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

struct DomAndRenderer
{
    QDomDocument dom;
    std::shared_ptr<QSvgRenderer> renderer;

    void reload()
    {
        renderer->load(dom.toByteArray());
    }
};

class DomElementSvgItem: public QGraphicsSvgItem
{
    public:
        DomElementSvgItem(const DomAndRenderer &dr, const QString &elementId, QGraphicsItem *parentItem = nullptr)
            : QGraphicsSvgItem(parentItem)
            , dr_(dr)
        {
            setSharedRenderer(dr_.renderer.get());
            setElementId(elementId);
            auto bounds = dr_.renderer->boundsOnElement(elementId);
            setPos(bounds.x(), bounds.y());
        }

        QDomElement getRootElement() const
        {
            return find_element_by_id(dr_.dom, elementId());
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

class MyFilterItem: public QGraphicsItemGroup
{
    public:
        using QGraphicsItemGroup::QGraphicsItemGroup;

        bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override
        {
            if (auto svgItem = dynamic_cast<DomElementSvgItem *>(watched))
            {
                if (event->type() == QEvent::GraphicsSceneHoverEnter)
                {
                    qDebug() << "hover enter";
                    svgItem->setFillColor("#008888");
                }

                if (event->type() == QEvent::GraphicsSceneHoverLeave)
                {
                    qDebug() << "hover leave";
                    svgItem->setFillColor("#ffffff");
                }
            }

            return false;
        }
};

static std::stringstream g_graphvizErrorBuffer;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    std::ifstream dotIn("foo.dot");
    std::stringstream dotBuf;
    dotBuf << dotIn.rdbuf();
    std::string dotStr(dotBuf.str());

#if 0
    g_graphvizErrorBuffer.str({}); // clear the buffer

    GVC_t *gvc = gvContext();

    auto myerrf = [] (char *msg) -> int
    {
        g_graphvizErrorBuffer << msg;
        return 0;
    };

    agseterrf(myerrf);

    agwarningf("my ag warning\n");
    agerrorf("my ag error\n");

    spdlog::info("graphviz errors: {}", g_graphvizErrorBuffer.str());

    Agraph_t *g = agmemread(dotStr.c_str());

    gvLayout(gvc, g, "dot");

    char *renderDest = nullptr;
    unsigned int renderSize = 0;
    gvRenderData(gvc, g, "svg", &renderDest, &renderSize);

    QByteArray svgData(renderDest);

    qDebug() << svgData.size();

    gvFreeRenderData(renderDest);
    gvFreeLayout(gvc, g);
    agclose(g);
    gvFreeContext(gvc);
#else
    auto svgData = mesytec::graphviz_util::layout_and_render_dot_q(
        dotStr.c_str(), "dot");
#endif

    DomAndRenderer dr = {};
    dr.renderer = std::make_shared<QSvgRenderer>(svgData);
    dr.dom.setContent(svgData);

    assert(dr.renderer->isValid());

    QGraphicsScene scene;

    auto myFilterItem = new MyFilterItem;
    scene.addItem(myFilterItem);

    // Loop over xml elements. For each StartElement check if it has an id
    // attribute. If so use it to create a QGraphicsSvgItem for that specific
    // element id and add the item to the scene.
    int nodesToDraw = 2000;
    QMap<QString, size_t> elementTypeCounts;
    QXmlStreamReader xmlReader(svgData);
    while (!xmlReader.atEnd() && nodesToDraw > 0)
    {
        switch (xmlReader.readNext())
        {
            case QXmlStreamReader::TokenType::StartElement:
            {
                auto attributes = xmlReader.attributes();

                auto elementClass = attributes.value("class").toString();

                QSet<QString> elementClassesToDraw = { "node", "edge" };

                if (!elementClassesToDraw.contains(elementClass))
                    continue;

                if (attributes.hasAttribute("id"))
                {
                    auto elementId = attributes.value("id").toString();
                    auto svgItem = new DomElementSvgItem(dr, elementId);
                    scene.addItem(svgItem);
                    --nodesToDraw;
                    ++elementTypeCounts[elementClass];
                    if (elementClass == "node")
                    {
                        svgItem->installSceneEventFilter(myFilterItem);
                        svgItem->setAcceptHoverEvents(true);
                    }
                }
            } break;

            default:
                break;
        }
    }

    qDebug() << "elementTypeCounts:" << elementTypeCounts;

    QGraphicsView view;
    view.setScene(&scene);
    view.setDragMode(QGraphicsView::ScrollHandDrag);
    view.setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    view.setRenderHints(
        QPainter::Antialiasing
        | QPainter::TextAntialiasing
        | QPainter::SmoothPixmapTransform
        | QPainter::HighQualityAntialiasing
        );

    view.setWindowTitle("dot -> svg -> one QGraphicsSvgItem");
    view.show();

    return app.exec();
}
