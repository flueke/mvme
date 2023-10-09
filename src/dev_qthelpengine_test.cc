#include <QApplication>
#include <QDir>
#include "util/qt_str.h"

#include <QtHelp>

#include "help_widget.h"

using namespace mesytec::mvme;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QString collectionFile;
    auto appPath = QCoreApplication::applicationDirPath();

    if (appPath.endsWith("build"))
    {
        collectionFile = appPath + "/doc/sphinx/qthelp/mvme.qhc";
    }
    else
    {
        collectionFile = appPath
            + QDir::separator() + ".." + QDir::separator() + QSL("doc") + QDir::separator() + QSL("mvme.qhc");
    }

    qDebug() << "collectionFile" << collectionFile;

    auto helpEngine = std::make_unique<QHelpEngine>(collectionFile);
    #if 0
    qDebug() << "setupData:" << helpEngine.setupData();

    auto contentModel = helpEngine.contentModel();
    auto contentWidget = helpEngine.contentWidget();
    auto indexModel = helpEngine.indexModel();
    auto indexWidget = helpEngine.indexWidget();
    auto searchEngine = helpEngine.searchEngine();

    auto helpDisplay = new QTextBrowser();

    QWidget mainWidget;
    auto layout = new QHBoxLayout(&mainWidget);
    layout->addWidget(contentWidget);
    layout->addWidget(indexWidget);
    layout->addWidget(helpDisplay);

    mainWidget.show();
    #endif

    HelpWidget helpWidget(std::move(helpEngine));
    helpWidget.show();

    return app.exec();
}
