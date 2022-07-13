#include <QApplication>
#include <QSplitter>
#include <fstream>
#include <sstream>
#include <QDebug>

#include "graphviz_util.h"
#include "qt_util.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    std::ifstream dotIn("bar.dot");
    dotIn.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    std::stringstream dotBuf;
    dotBuf << dotIn.rdbuf();

    mesytec::graphviz_util::DotWidget dotWidget;
    add_widget_close_action(&dotWidget);

    dotWidget.setDot(dotBuf.str());
    dotWidget.resize(1400, 800);
    dotWidget.show();

    if (auto lrSplitter = dotWidget.findChild<QSplitter *>("lrSplitter"))
    {
        int w = 0.5 * lrSplitter->width();
        lrSplitter->setSizes({w, w});
    }

    return app.exec();
}
