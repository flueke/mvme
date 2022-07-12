#include <QApplication>

#include "graphviz_util.h"
#include "qt_util.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    mesytec::graphviz_util::DotWidget dotWidget;
    add_widget_close_action(&dotWidget);

    dotWidget.show();

    return app.exec();
}
