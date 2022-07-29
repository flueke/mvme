#include <fstream>
#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <QApplication>
#include <QPixmap>
#include <QLabel>
#include <QDebug>
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
    gvRenderData(gvc, g, "png", &renderDest, &renderSize);

    QPixmap pixmap;
    pixmap.loadFromData(reinterpret_cast<const unsigned char *>(renderDest), renderSize, "png");

    gvFreeRenderData(renderDest);

    gvFreeLayout(gvc, g);

    agclose(g);
    gvFreeContext(gvc);


    qDebug() << pixmap.size() << pixmap.depth();

    QLabel label;
    label.setWindowTitle("graphviz rendered png output");
    label.setPixmap(pixmap);
    label.show();
    return app.exec();
}
