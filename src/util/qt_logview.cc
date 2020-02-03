#include "util/qt_logview.h"
#include <QMenu>

std::unique_ptr<QPlainTextEdit> make_logview(size_t maxBlockCount)
{
    auto result = std::make_unique<QPlainTextEdit>();

    result->setAttribute(Qt::WA_DeleteOnClose);
    result->setReadOnly(true);
    result->setWindowTitle("Log View");
    QFont font("MonoSpace");
    font.setStyleHint(QFont::Monospace);
    result->setFont(font);
    result->setTabChangesFocus(true);
    result->document()->setMaximumBlockCount(maxBlockCount);
    result->setContextMenuPolicy(Qt::CustomContextMenu);
    result->setStyleSheet("background-color: rgb(225, 225, 225);");

    auto raw = result.get();

    QObject::connect(
        raw, &QWidget::customContextMenuRequested,
        raw, [=](const QPoint &pos)
    {
        auto menu = raw->createStandardContextMenu(pos);
        auto action = menu->addAction("Clear");
        QObject::connect(action, &QAction::triggered, raw, &QPlainTextEdit::clear);
        menu->exec(raw->mapToGlobal(pos));
        menu->deleteLater();
    });

    return result;
}
