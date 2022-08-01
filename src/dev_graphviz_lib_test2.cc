#include <cassert>
#include <fstream>
#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsSvgItem>
#include <QGraphicsView>
#include <QSvgRenderer>
#include <QXmlStreamReader>
#include <sstream>

// DOT -> layout -> svg -> individual QGraphicsSvgItems created using the same shared renderer

// Approach:
// load DOT code -> use graphviz to render to svg -> use QXmlStreamReader and
// QSvgRenderer to load and render the svg data. Use the xml reader to get the
// ids of svg items that should be rendered.
// For each item create a QGraphicsSvgItem using the shared renderer.
// Problem: cannot change attributes like the background color of the svg
// graphics items. QGraphicsSvgItem does not expose any functionality to
// influcence the drawing (probably as the rendering is done by the svg renderer).

class MyFilterItem: public QGraphicsItemGroup
{
    public:
        using QGraphicsItemGroup::QGraphicsItemGroup;

        bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override
        {
            auto svgItem = dynamic_cast<QGraphicsSvgItem *>(watched);
            //qDebug() << __PRETTY_FUNCTION__ << watched << event;
            if (event->type() == QEvent::GraphicsSceneHoverEnter)
            {
                qDebug() << "hover enter";
                //watched->setBrush(QBrush("#0000ff"));
            }

            if (event->type() == QEvent::GraphicsSceneHoverLeave)
            {
                qDebug() << "hover leave";
            }

            return false;
        }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (argc < 2)
        return 1;

    std::ifstream dotIn(argv[1]);
    std::stringstream dotBuf;
    dotBuf << dotIn.rdbuf();
    std::string dotStr(dotBuf.str());

    GVC_t *gvc = gvContext();
    Agraph_t *g = agmemread(dotStr.c_str());

    gvLayout(gvc, g, "dot");

    // render the graph in png format into a memory buffer.
    char *renderDest = nullptr;
    unsigned int renderSize = 0;
    gvRenderData(gvc, g, "svg", &renderDest, &renderSize);

    QXmlStreamReader xmlReader(renderDest);
    QSvgRenderer svgRenderer(&xmlReader);

    assert(svgRenderer.isValid());

    // Reset the reader and add the original xml data again.
    xmlReader.clear();
    xmlReader.addData(renderDest);

    gvFreeRenderData(renderDest);

    gvFreeLayout(gvc, g);

    agclose(g);
    gvFreeContext(gvc);

    QGraphicsScene scene;

    auto myFilterItem = new MyFilterItem;
    scene.addItem(myFilterItem);

    // Loop over xml elements. For each StartElement check if it has an id
    // attribute. If so use it to create a QGraphicsSvgItem for that specific
    // element id and add the item to the scene.
    int nodesToDraw = 100000000;
    QMap<QString, size_t> elementTypeCounts;
    while (!xmlReader.atEnd() && nodesToDraw > 0)
    {
        switch (xmlReader.readNext())
        {
            case QXmlStreamReader::TokenType::StartElement:
            {
                auto attributes = xmlReader.attributes();

                auto elementClass = attributes.value("class").toString();

                QSet<QString> elementTypesToDraw = { "node", "edge" };

                if (!elementTypesToDraw.contains(elementClass))
                    continue;

                if (attributes.hasAttribute("id"))
                {
                    auto elementId = attributes.value("id").toString();
                    //qDebug() << "elementId=" << elementId;
                    auto svgItem = new QGraphicsSvgItem;
                    svgItem->setSharedRenderer(&svgRenderer);
                    svgItem->setElementId(elementId);
                    auto elementBounds = svgRenderer.boundsOnElement(elementId);
                    //qDebug() << "elementBounds" << elementBounds;
                    svgItem->setPos(elementBounds.x(), elementBounds.y());
                    scene.addItem(svgItem);
                    --nodesToDraw;
                   ++elementTypeCounts[elementClass];
                   svgItem->installSceneEventFilter(myFilterItem);
                   svgItem->setAcceptHoverEvents(true);
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
