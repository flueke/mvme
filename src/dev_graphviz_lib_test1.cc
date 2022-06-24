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

    // render the graph in png format into a memory buffer.
    char *renderDest = nullptr;
    unsigned int renderSize = 0;
    gvRenderData(gvc, g, "svg", &renderDest, &renderSize);

    QXmlStreamReader xmlReader(renderDest);
    QSvgRenderer svgRenderer(&xmlReader);

    assert(svgRenderer.isValid());

    gvFreeRenderData(renderDest);

    gvFreeLayout(gvc, g);

    agclose(g);
    gvFreeContext(gvc);

    auto svgItem = new QGraphicsSvgItem;
    svgItem->setSharedRenderer(&svgRenderer);

#if 0
    QFile svgIn("foo.svg");
    if (!svgIn.open(QIODevice::ReadOnly))
        return 1;

    QXmlStreamReader xmlReader2(&svgIn);
    QSvgRenderer svgRenderer2(&xmlReader2);
    auto svgItem2 = new QGraphicsSvgItem;
    svgItem2->setSharedRenderer(&svgRenderer2);
#endif

    QGraphicsScene scene;
    scene.addItem(svgItem);

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
