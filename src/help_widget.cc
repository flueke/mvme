#include "help_widget.h"
#include <QBoxLayout>
#include <QSplitter>
#include <QtHelp>

namespace mesytec::mvme
{

HelpBrowser::HelpBrowser(QHelpEngine *helpEngine, QWidget *parent)
    : QTextBrowser(parent)
    , helpEngine_(helpEngine)
{
}

QVariant HelpBrowser::loadResource(int type, const QUrl &name)
{
    qDebug() << "loadResource" << name;
    QVariant result;
    if (name.scheme() == "qthelp")
        result = QVariant(helpEngine_->fileData(name));
    else
        result = QTextBrowser::loadResource(type, name);

    if (!result.isValid() || result.isNull())
    {
        qDebug() << "loadResource: url =" << name << ", result =" << result;
    }

    return result;
}

struct HelpWidget::Private
{
    std::unique_ptr<QHelpEngine> helpEngine_;
    HelpBrowser *helpBrowser_;
};

HelpWidget::HelpWidget(std::unique_ptr<QHelpEngine> &&helpEngine, QWidget *parent)
    : QWidget(parent)
    , d(std::make_unique<Private>())
{
    d->helpEngine_ = std::move(helpEngine);
    d->helpEngine_->setupData();
    d->helpBrowser_ = new HelpBrowser(d->helpEngine_.get());

    auto contentWidget  = d->helpEngine_->contentWidget();
    auto indexWidget    = d->helpEngine_->indexWidget();
    auto searchEngine   = d->helpEngine_->searchEngine();

    auto navWidget = new QWidget;
    auto navLayout = new QVBoxLayout(navWidget);
    navLayout->addWidget(contentWidget);
    navLayout->addWidget(indexWidget);

    auto splitter = new QSplitter;
    splitter->addWidget(navWidget);
    splitter->addWidget(d->helpBrowser_);

    auto layout = new QHBoxLayout(this);
    layout->addWidget(splitter);


    connect(contentWidget, &QHelpContentWidget::linkActivated,
        this, [this] (const QUrl &url) {
             qDebug() << "url =" << url;
             d->helpBrowser_->setSource(url);
        });


}

HelpWidget::~HelpWidget() { }


}
