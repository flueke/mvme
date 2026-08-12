#include "run_notes_widget.hpp"
#include "util/markdownhighlighter.h"
#include "util/qt_monospace_textedit.h"

#include <QDesktopServices>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace mesytec::mvme
{

struct RunNotesWidget::Private
{
    RunNotesWidget *q = nullptr;
    MarkdownHighlighter *highlighter = nullptr;
    QPlainTextEdit *editor = nullptr;
    QTextBrowser *view = nullptr;
    QSplitter *splitter = nullptr;
    QStatusBar *statusbar = nullptr;
};

RunNotesWidget::RunNotesWidget(QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    d->editor = util::make_monospace_plain_textedit().release();
    d->view = new QTextBrowser(this);
    d->view->setOpenExternalLinks(true);
    d->view->setReadOnly(true);
    d->highlighter = new MarkdownHighlighter(d->editor->document());
    d->splitter = new QSplitter(Qt::Horizontal, this);
    d->statusbar = new QStatusBar(this);

    d->splitter->addWidget(d->editor);
    d->splitter->addWidget(d->view);

    auto l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(d->splitter);
    l->addWidget(d->statusbar);
    l->setStretch(0, 1.0);
    l->setStretch(1, 0.0);

    connect(d->editor, &QPlainTextEdit::textChanged, this,
            [this]()
            {
                auto text = this->toMarkdown();
                d->view->setMarkdown(text);
                emit textChanged(text);
            });
}

RunNotesWidget::~RunNotesWidget() {}

QString RunNotesWidget::toMarkdown() const { return d->editor->toPlainText(); }

void RunNotesWidget::setMarkdown(const QString &text) { d->editor->setPlainText(text); }

void RunNotesWidget::setReadOnly(bool readOnly)
{
    QString css = readOnly ? "background-color: rgb(225, 225, 225);" : "";
    d->editor->setStyleSheet(css);
    d->editor->setReadOnly(readOnly);
}

} // namespace mesytec::mvme
