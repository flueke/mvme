#include <QApplication>
#include <QWebEngineView>

#include "mvme_session.h"

//using namespace mesytec::mvme;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    mvme_init("dev_mvme_qwebengine");

    QWebEngineView *view = new QWebEngineView();
    view->setAttribute(Qt::WA_DeleteOnClose);
    view->load(QUrl("http://www.qt.io/"));
    view->show();

    return app.exec();
}
