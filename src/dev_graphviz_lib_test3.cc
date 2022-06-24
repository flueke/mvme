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

struct DomAndRenderer
{
    QDomDocument dom;
    std::shared_ptr<QSvgRenderer> renderer;

    void reload()
    {
        renderer->load(dom.toByteArray());
    }
};

static const std::set<QString> SvgBasicShapes = { "circle", "ellipse", "line", "polygon", "polyline", "rect" };

// Depth first search starting at the children of the given root node.
QDomElement find_first_basic_svg_shape_element(const QDomNode &root)
{
    auto n = root.firstChild();

    while (!n.isNull())
    {
        if (n.isElement())
        {
            auto e = n.toElement();
            if (SvgBasicShapes.count(e.tagName()))
                return e;
        }

        if (n.hasChildNodes())
        {
            auto e = find_first_basic_svg_shape_element(n);
            if (!e.isNull())
                return e;
        }

        n = n.nextSibling();
    }

    return {};
}

QDomElement find_element_by_id(const QDomNode &root, const QString &id)
{
    auto predicate = [&id](const QDomNode &n)
    {
        if (n.isElement())
        {
            auto e = n.toElement();
            return (e.hasAttribute("id") && e.attribute("id") == id);
        }

        return false;
    };

    if (predicate(root))
        return root.toElement();;

    auto n = root.firstChild();

    while (!n.isNull())
    {
        if (predicate(n))
            return n.toElement();

        auto ret = find_element_by_id(n, id);

        if (!ret.isNull())
            return ret;

        n = n.nextSibling();
    }

    return {};
}

QDomElement find_element_by_id(const QDomDocument &doc, const QString &id)
{
    return find_element_by_id(doc.documentElement(), id);
}

class MyGraphicsSvgItem: public QGraphicsSvgItem
{
    public:
        MyGraphicsSvgItem(const DomAndRenderer &dr, const QString &elementId, QGraphicsItem *parentItem = nullptr)
            : QGraphicsSvgItem(parentItem)
            , dr_(dr)
        {
            setSharedRenderer(dr_.renderer.get());
            setElementId(elementId);
            auto bounds = dr_.renderer->boundsOnElement(elementId);
            setPos(bounds.x(), bounds.y());
        }

        void setFillColor(const QColor &c)
        {
            auto localRoot = find_element_by_id(dr_.dom, elementId());

            if (!localRoot.isNull())
            {
                auto shapeElement = find_first_basic_svg_shape_element(localRoot);

                if (!shapeElement.isNull())
                {
                    shapeElement.setAttribute("fill", c.name());
                    dr_.reload();
                }
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
            if (auto svgItem = dynamic_cast<MyGraphicsSvgItem *>(watched))
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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    std::ifstream dotIn("foo.dot");
    std::stringstream dotBuf;
    dotBuf << dotIn.rdbuf();
    std::string dotStr(dotBuf.str());

    GVC_t *gvc = gvContext();
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
                    auto svgItem = new MyGraphicsSvgItem(dr, elementId);
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
