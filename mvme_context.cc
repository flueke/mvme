#include "mvme_context.h"
#include "vme_module.h"

#include <QListWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QLabel>

void MVMEContext::addModule(VMEModule *module)
{
    modules.push_back(module);
    emit moduleAdded(module);
}

struct MVMEContextWidgetPrivate
{
    MVMEContextWidgetPrivate(MVMEContextWidget *q, MVMEContext *context)
        : m_q(q)
        , context(context)
    {}

    void foo();

    MVMEContextWidget *m_q;
    MVMEContext *context;

    QLabel *controller_widget;
    QListWidget *lw_modules;
    QListWidget *lw_chains;
    QListWidget *lw_stacks;
};

MVMEContextWidget::MVMEContextWidget(MVMEContext *context, QWidget *parent)
    : QWidget(parent)
    , m_d(new MVMEContextWidgetPrivate(this, context))
{
    m_d->controller_widget = new QLabel("Controller!\nimplement me!");

    m_d->lw_modules = new QListWidget;
    m_d->lw_chains = new QListWidget;
    m_d->lw_stacks = new QListWidget;

    auto splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(m_d->controller_widget);

    auto w = new QWidget;
    auto wl = new QVBoxLayout(w);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    wl->addWidget(new QLabel("Modules"));
    wl->addWidget(m_d->lw_modules);
    splitter->addWidget(w);

    w = new QWidget;
    wl = new QVBoxLayout(w);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    wl->addWidget(new QLabel("Chains"));
    wl->addWidget(m_d->lw_chains);
    splitter->addWidget(w);

    w = new QWidget;
    wl = new QVBoxLayout(w);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);
    wl->addWidget(new QLabel("Stacks"));
    wl->addWidget(m_d->lw_stacks);
    splitter->addWidget(w);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(context, &MVMEContext::moduleAdded, this, &MVMEContextWidget::onModuleAdded);
}

void MVMEContextWidget::onModuleAdded(VMEModule *module)
{
    auto item = new QListWidgetItem(module->getName());
    item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(module)));
    m_d->lw_modules->addItem(item);
}
