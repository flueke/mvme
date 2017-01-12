#include "gui_util.h"
#include <QFile>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QTextStream>
#include <QWidget>

#define QSL(str) QStringLiteral(str)

QWidget *make_vme_script_ref_widget()
{
    QWidget *widget = nullptr;

    QFile inFile(":/vme-script-help.html");
    if (inFile.open(QIODevice::ReadOnly))
    {
        auto tb = new QTextBrowser;
        QTextStream inStream(&inFile);
        tb->document()->setHtml(inStream.readAll());

        // scroll to top
        auto cursor = tb->textCursor();
        cursor.setPosition(0);
        tb->setTextCursor(cursor);

        widget = new QWidget;
        widget->setWindowTitle(QSL("VME Script Reference"));
        auto layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(tb);
    }

    return widget;
}
