#include "dev_graphviz_lib_test5.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    MainWindow mainwin;
    mainwin.show();

    int ret = app.exec();
    return ret;
}

