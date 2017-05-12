#include "gui_util.h"
#include <QDebug>
#include <QFile>
#include <QHBoxLayout>
#include <QPainter>
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
        widget->setObjectName("VMEScriptReference");
        widget->setWindowTitle(QSL("VME Script Reference"));
        auto layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(tb);
    }

    return widget;
}

QPixmap embellish_pixmap(const QString &original_source, const QString &embellishment_source)
{
    QPixmap result(original_source);
    QPixmap embellishment(embellishment_source);
    QRect target_rect(result.width() / 2, result.height() / 2, result.width() / 2, result.height() / 2);
    QPainter painter(&result);
    painter.drawPixmap(target_rect, embellishment, embellishment.rect());
    return result;
}
